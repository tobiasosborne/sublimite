# edit-xt90-s12 — W2.2 journal on os.h

Verdict: **done**. Final release build and full Linux/Windows checks passed.
Windows editor-dependent fixtures have explicit skips, as allowed by the brief.

## Scope and changes

The inventory was written before implementation in
`docs/decisions/edit-xt90.md`; the raw pre-edit scan is
`build/worker-logs/inventory-before.txt`. The decision document gives every
file/call family, frozen entry, and applicable migration rule.

- Journal transport, replay, retention, rotation, retirement, session creation
  and manual replay use `os_file`, `os_dir`, and the frozen OS entries.
  `cache_write` uses exactly one `os_file_pwrite_once`; native error capture,
  sticky IO, short-write accounting and worker drain remain intact.
- Data/full-file syncs use writable handles. Directory syncs use `os_dir_sync`.
  Clone NOTSUP and the pre-existing other-error fallback use bounded worker
  copying. Every private temporary is created exclusively.
- Shared rotation/retention and exclusive retirement guards retain their
  existing lifetimes without lock upgrades. Held/named comparisons use full
  OS identities. Acknowledgment scans the pinned file directly.
- Portable session APIs coexist with Linux descriptor adapters. A consumer
  uses `os_file_dup` for lease handoff; the test proves last-duplicate lock
  lifetime. The actual editor descriptor handoff remains outside scope.
- Windows path normalization preserves drive/UNC paths and drive roots.
  Directory-as-file classification follows the existing journal outcomes.
  The manual publisher's checked close uses `os_file_to_stdio` plus `fclose`,
  because the frozen raw close has no result.
- Windows builds include journal and its dependency closure, portable
  integration cases, acknowledgment CLI cases and the seeded process-death
  campaign. Termination uses `os_proc_spawn`/`os_proc_kill`/`os_proc_wait`.

Files touched: `src/journal/{journal.c,journal.h,manual.c,path.c,retire.c,
session.c,private.h,journal_posix.c,journal_win.c}`, `mk/win.mk`,
`tests/{journal_os_test.c,journal_hot_exit_test.c}`,
`tests/win/{journal_ack_test.c,journal_kill_test.c}`, this report and the
bead decision document. No editor, file, savectl or os.h edits. No state-changing
git command, bd command, main-checkout edit, or subagent was used.

## Red / green

All measured results here are (M) [AC, desktop Spiral]. BAT0 was absent before
measurement. Initial `uptime`: `11:51:43 up 4:08, load average: 0.47, 1.07, 1.50`.
Numeric fields in quoted test output are measured by the named command;
fixture counts and limits are inherited from the cited test source, rather
than new budgets.

The brief allows the frozen-header module compile as the migration red.
The initial build list did not include journal, so the red explicitly selects
it; the final build includes it normally.

```text
make TARGET=win -j64 win-compile WIN_COMPILE_MODULES=journal
exit=2
src/journal/session.c:31:18: error: call to undeclared function 'mkostemps'
src/journal/session.c:31:61: error: use of undeclared identifier 'O_CLOEXEC'
src/journal/manual.c:74:12: error: no member named 'st_mtim' in 'struct stat'
src/journal/retire.c:119:35: error: use of undeclared identifier 'LOCK_NB'
make: *** [mk/win.mk:46: build/win/rel/src/journal/journal.o] Error 1

make TARGET=win -j64 win-compile
exit=0
```

Full logs: `build/worker-logs/{red-win-compile,green-win-compile}.log`.
The existing journal suites are the green for the individual migrated call
families (§9.3, §9.4, §9.9, §9.10, §9.12); their original Linux expectations
and source files are unchanged.

The new directory-target regression was also exercised red against the
migration's earlier unclassified open-error branch, then green with the
classification fix restored:

```text
DISPLAY=:98 build/tests/journal_os_test
exit=1
journal_os_test:61 FAIL journal_manual_replay(path,dir,1,false,output)==JOURNAL_INVALID (native=21)

DISPLAY=:98 build/tests/journal_os_test
exit=0
journal_os_test: malloc_guard=active append_allocations=0
journal_os_test: guarded_appends=10000 append_allocations=0
journal_os_test: EINTR/errors/zero/short writes are single-attempt sticky IO without allocations
journal_os_test: all portable journal cases passed
```

Logs: `build/worker-logs/{red-directory,green-directory}.log`. The guarded
append count comes from the existing journal_test fixture (P: that campaign
is reused verbatim). The new portable release suite additionally checks the
allocator positive control; sanitizer builds explicitly report their inert
guard instead of pretending to measure allocations.

## Linux verification

The first sandboxed check failed in base_test with LeakSanitizer's ptrace
fatal diagnostic. It was rerun with the sandbox restriction lifted and
LeakSanitizer enabled; no `detect_leaks=0` was used. The intermediate full
check passed (exit 0, `check: 105 test binaries passed`, M: make's test list at
that invocation). The final check includes both newly added portable suites.

Final `make -j64 all`: exit 0 (M: `linux-final-all.exit`).
Final full `DISPLAY=:98 make -j64 check`: **exit 0**; `check: 106 test binaries passed` (M: final make list and captured exit file). LeakSanitizer remained enabled.

Final command logs: `build/worker-logs/linux-final-all.log`,
`build/worker-logs/linux-final-check.log` and matching `.exit` files.
Final check start [AC, desktop Spiral]: `12:13:48 up 4:30, load average:
7.42, 6.54, 4.53` (M: captured uptime).

Release journal guard (after migration):

```text
DISPLAY=:98 build/tests/journal_test
exit=0
journal_test: malloc_guard=active append_allocations=0
journal_test: ok (roundtrip, corruption, torn pages, straddles, base conflict, rotation, allocator, back-pressure)

DISPLAY=:98 build/tests/journal_hot_exit_test
exit=0
journal_hot_exit_test: cache/durable/group/PAINT/rotation/copy process-death recovery passed
```

The full run exercises by name `journal_test`, `journal_ack_test`,
`journal_group_test`, `journal_kill_test`, `journal_retire_test`,
`editor_checkpoint_test` (f3i), `editor_session_test` (dzk1),
`editor_recovery_test`, and `dataloss_test` (ze59), including rotation under
shared directory locks, retirement sweeps and interrupted copies. Selected
final evidence follows.

The required journal benchmark was run with its existing fixtures; correctness
and allocation output are checked, timing misses remain tracked observations.
Final `DISPLAY=:98 build/bench/journal_bench`: exit 0 (M: `journal-final-bench.exit`), correctness OK and no tracked component misses. Its BAT0-only detector prints `unknown` on this desktop;
the enclosing log records the actual AC/desktop condition and `uptime`.

## Windows verification

The first normal combined check passed the journal release and sanitizer
suites, including the deterministic TerminateProcess boundaries and inherited
seeded campaign. It then failed the sanitizer OS reparse case because its
shared `%TEMP%\\sublimite-test-reparse` fixture had disappeared (`OS_E_NOENT`).
The same OS reparse case had passed in release. Another concurrent Windows
check can remove that globally named fixture; this is a harness collision,
not a journal failure.

The final `make TARGET=win -j64 check` runs with a unique Windows TEMP/TMP
subdirectory forwarded through WSLENV; the existing Windows fixture helper
creates that parent, and Windows cmd removes it afterwards. Native Linux
compiler scratch stays in `TMPDIR=/tmp`. No Linux process writes to NTFS.
An initial isolated attempt omitted TMPDIR and failed cross-compilation
before tests (clang could not create its temporary file); setting TMPDIR
corrected that harness environment. This does not change production code or
the shared fixture tool.

Exact reproducible runner: `build/worker-logs/windows-final-check.sh`.
Final log/exit: `build/worker-logs/windows-final-check.{log,exit}`.
[AC, desktop Spiral] start: `12:14:08 up 4:30, load average:
6.36, 6.35, 4.51` (M: captured uptime).

Final `make TARGET=win -j64 check`: exit 0 (M: captured exit file). All
summary counts below are (M), printed by the check's release/sanitizer lists.
The kill trial/script fixture is inherited from tests/journal_kill_test.c (P).
The allocator measurement is meaningful in the release build; sanitizer output
explicitly says inactive.

```text
journal_hot_exit_test: cache/durable/group/PAINT/rotation/copy process-death recovery passed
journal_kill_test: ok trials=50 script_edits=100000 issued=2661892 page_cache=2661864 replayed=2661879 acknowledged=2618971 (page_cache<=replayed<=issued; piece_tree==independent_byte_model_at_replayed; trial0=kill_before_pump)
os_win_test: 18/19 passed, 1 unavailable
os_test: 17 cases, 0 failed
journal_os_test: malloc_guard=active append_allocations=0
journal_os_test: guarded_appends=10000 append_allocations=0
journal_hot_exit_test: cache/durable/group/PAINT/rotation/copy process-death recovery passed
journal_kill_test: ok trials=50 script_edits=100000 issued=2661661 page_cache=2661625 replayed=2661644 acknowledged=2618971 (page_cache<=replayed<=issued; piece_tree==independent_byte_model_at_replayed; trial0=kill_before_pump)
os_win_test: 18/19 passed, 1 unavailable
os_test: 17 cases, 0 failed
journal_os_test: malloc_guard=sanitizer-inactive append_allocations=0
journal_os_test: guarded_appends=10000 append_allocations=0
ASan fired: ok
Windows journal: SKIP journal_group_test, journal_retire_test, editor_session_test, editor_recovery_test, dataloss_test (require editor/platform modules)
Windows journal: SKIP legacy journal_test file-save fixtures (require src/file); journal_os_test covers portable journal cases
Windows check: 6 release + 6 sanitizer test binaries passed; ASan negative control passed
```

Windows checks use `tools/winrun.sh`. WSL interop's first sandboxed launch
failed `UtilBindVsockAnyPort: socket failed 1`; execution succeeds with the
sandbox restriction lifted. Windows LeakSanitizer is unsupported by the
existing target; ASan/UBSan and the intentional ASan-failure control run.

Explicit Windows skips: the original editor-dependent group, retirement,
session/recovery and data-loss suites; the legacy journal_test file-save
fixtures (file module not in this target); acknowledgment hard-link/symlink
creation (no frozen os.h entry). The journal-only integration, retirement
primitives, checked CLI, deterministic kill boundaries and seeded kill oracle
run as native Windows executables with NTFS scratch.

## Found, not fixed / coordinator integration

- `src/editor/recovery.c:760` still duplicates `editor_config.session_lease`
  with `F_DUPFD_CLOEXEC`; editor.h uses `const int *`, and open.c closes the
  int session/source leases. These are explicitly outside this bead. The
  editor worker must adopt the portable session APIs and `os_file_dup` in
  the same ownership handoff; Linux callers continue through the adapters.
  Main's creator/cleanup calls likewise retain their Linux descriptor API.
- Editor recovery still opens/scans/locks source journals itself, and its
  parent barriers remain POSIX. Savectl has independent descriptor-based
  reload caches and parent synchronization; it does not access journal's
  now-private handles. These await their own migration beads.
- The existing BASE wire schema cannot represent a nonzero high file-index
  word. Capture refuses it safely; rotation falls back to conservative names.
  A wider BASE format, if required for filesystems beyond the tested NTFS
  case, needs a separate compatibility decision coordinated with file_id.
- Merge `WIN_MODULES`, `WIN_TESTS`, and the win-compile addition as a union
  with W0.5's parallel changes, preserving these journal harnesses.
- The shared Windows reparse fixture tool still has a global scratch name.
  The private-TEMP verification workaround avoids that collision; a generic
  per-run fixture fix belongs to the build/OS harness owner.

No coordinator design choice is needed for the tested NTFS journal behavior.
The wider identity schema is deliberately deferred; Windows editor hot exit
still needs the already-planned editor/file/savectl integration work.

## Rule-specific green coverage

| Rule | Existing unchanged Linux green / portable Windows green |
|---|---|
| §9.1 directory opened as file | journal_test open-options/BASE conflict cases; journal_os_test directory-open/manual-target regression; Windows acknowledgment directory-source BUSY case |
| §9.3 timestamps | journal_test BASE capture/check/replay and retained-generation validation; journal_os_test BASE conflict, copy validation and manual replacement |
| §9.4 one append attempt | journal_test page-cache, EAGAIN/ENOSPC/EIO/EINTR/short-write/gap/typing allocator cases; journal_os_test single-attempt error matrix and counting allocator; both kill oracles |
| §9.9 exclusive temporaries | journal_test rotation-name, directory pin, long-name, save/barrier/copy cases; journal_os_test replacement plus manual publication; process-death rotation and copy cases |
| §9.10 locks/dup | journal_retire_test rotation admission and dead-copy sweep; editor_session_test existing lease behavior; portable journal_os_test dup/creator-close/other-opener/retirement/release checks; process-death guard release |
| §9.12 writable syncs | journal_test save metadata, retention, retry and barrier suites; journal_os_test retained copy/manual publisher; Windows runs use real FlushFileBuffers through RDWR handles |

Benchmark evidence (M) [AC, desktop Spiral], concurrent verification load,
`uptime`: `12:14:33 up 4:30, load average: 5.71, 6.20, 4.51`. The raw benchmark
labels below reflect its unchanged BAT0-only detector; the preceding machine
stamp supplies the actual power condition. These are observations, not new
budgets or complete input-to-frame claims.

```text
TRACK journal_append payload=1 edits=100000 p50_ns=984 p99_ns=9250 gate_p99_ns=20000_(G) within_gate=1 ui_page_cache_write=1 timer_ms=5 final_flush=1 bulk_jobs=60 bulk=index_scan/find_scan/save_write_standins (M)[unknown] load1=5.71
TRACK journal_append payload=1024 edits=100000 p50_ns=1181 p99_ns=14368 gate_p99_ns=20000_(G) within_gate=1 ui_page_cache_write=1 timer_ms=5 final_flush=1 bulk_jobs=60 bulk=index_scan/find_scan/save_write_standins (M)[unknown] load1=5.91
journal_bench: correctness=ok mode=GATE gate_misses=0 status=0 (complete G1/G9 endpoints require editor harness)
```

## Final Linux evidence

Final full command: `DISPLAY=:98 make -j64 check`, exit 0 (M), outside the
ptrace sandbox with LeakSanitizer enabled. Test counts below derive from make's
actual final test list; campaign totals derive from the test's recorded
progress. Existing f3i, ze59 and dzk1 suites ran by name as part of this full
command. The log's intentional negative CLI/lock subprocess diagnostics are
followed by their successful parent assertions; they are not omitted failures.

```text
editor_checkpoint_test: pending COPY checkpoint/crash/recovery/save, edit-28u referencing checkpoints, edit-f3i post-save checkpoints passed
dzk1: a tab killed in its first paint after restore is set aside on the next launch; its text survives
journal_ack_test: explicit verified acknowledgement releases reserved source and orphan BASE cleanup
journal_group_test: legacy, PAD, all byte cuts, corrupt commit, keystroke wire cost passed
journal_group_test: undo/redo/replacement/paste boundary crashes passed
journal_hot_exit_test: cache/durable/group/PAINT/rotation/copy process-death recovery passed
journal_kill_test: ok trials=50 script_edits=100000 issued=2662581 page_cache=2662534 replayed=2662552 acknowledged=2564096 (page_cache<=replayed<=issued; piece_tree==independent_byte_model_at_replayed; session_values_verified=1; trial0=kill_before_pump)
journal_os_test: malloc_guard=sanitizer-inactive append_allocations=0
journal_os_test: guarded_appends=10000 append_allocations=0
journal_retire_test: passed
journal_test: malloc_guard=ASan-inactive (release run required) append_allocations=0
journal_test: ok (roundtrip, corruption, torn pages, straddles, base conflict, rotation, allocator, back-pressure)
check: 106 test binaries passed
```
