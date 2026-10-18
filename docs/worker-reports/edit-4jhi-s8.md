# edit-4jhi worker report (s8)

Implemented recovery-3 F6 within this worktree. Lazy recovery acquires and
replays the recorded BASE generation, then binds TARGET independently. Valid
retained-copy recovery preserves edits and modified state without setting the
source aside. The existing origin/BASE identity check remains intact.

A separate existing COPY adoption failure for length-changing lazy replay
remains outside this bead; details below. The length-changing F6 regression
uses MMAP; COPY coverage uses a same-length edit against a rewritten TARGET.

## Files touched

- `src/editor/restore.c`: acquire named BASE; defer save setup while adopting
  unpublished replacements; bind the replayed text to TARGET; watch TARGET.
- `src/editor/session.c`: save setup captures an independently bound TARGET
  identity while retaining the original source guard, then rebases the newly
  created save controller to that destination identity.
- `tests/editor_session_test.c`: sparse retained-copy regression, focused
  `--edit-4jhi` selector, rewritten-TARGET COPY coverage, destination/source
  identity assertions, and recovery of the replacement checkpoint.
- `docs/decisions/edit-4jhi.md`: choices and scope boundary.
- `docs/worker-reports/edit-4jhi-s8.md`: this report.
- Ignored verification artifacts: `build/verification-edit-4jhi/` logs and
  the environment/cleanup wrapper `run.sh`.

## Evidence conventions

(P) The BASE size is the existing `JOURNAL_STARTUP_SCAN_BYTES + 1` byte,
105,000,000 + 1 = 105,000,001 bytes. The inserted marker has `strlen("x")`
bytes, so mapped recovered length is 105,000,002 bytes. The COPY case also
removes the last zero byte, preserving its original length. Marker/prefix
sizes derive from string lengths. Threshold one maps any nonempty file;
`UINT64_MAX` selects COPY for every representable fixture size.

(P) Command settings are the brief's existing build parallelism, test display,
lock timeout, and leak option. Exit zero is API/process success; red exit one
is the expected failed assertion. The allocator guard's key count is the
existing test workload. File/line numbers, versions and bead names identify
existing interfaces/artifacts rather than new budgets. All captured process,
power, uptime, load, disk-space and test observations below are (M)[AC]; the
power states were Charging or Not charging. No new duration or count budget
was introduced.

## Red run

The test was written before implementation. After correcting its
`piece_read` assertion to use the API's return-code contract, the original
production files were restored temporarily and this red run was captured.
The subsequent extra coverage adds rewritten TARGET and checkpoint relaunch.

Command (wrapper expands to the required private runtime directory,
`ASAN_OPTIONS=detect_leaks=1`, `DISPLAY=:97`,
`EDIT_DISPLAY_LOCK_TIMEOUT=1800`, and `sh tools/with_display_lock.sh`):

```sh
DISPLAY=:97 sh build/verification-edit-4jhi/run.sh build/san/tests/editor_session_test --edit-4jhi
```

```text
Charging
 12:18:15 up 6 days, 17:06,  1 user,  load average: 55.02, 56.20, 41.01
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  402G   54G  89% /
edit-4jhi: deferred retained BASE reopen text=no modified=no set-aside=yes target=yes
edit-4jhi: len=105000001 expected=105000002 prefix=base path=/home/tobias/Projects/sublimite/.wt/edit-4jhi-s8/build/editor-4jhi-w9rHxo/target save_path=/home/tobias/Projects/sublimite/.wt/edit-4jhi-s8/build/editor-4jhi-w9rHxo/target notice=file changed on disk since this session; its unsaved edits were set aside
editor_session_test:1285: FAIL text && dirty && !aside && bound
exit=1
Charging
 12:18:15 up 6 days, 17:06,  1 user,  load average: 55.02, 56.20, 41.01
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  402G   54G  89% /
```

## Green run

Same focused command, final code and regression coverage:

```text
Not charging
 12:22:10 up 6 days, 17:10,  1 user,  load average: 27.86, 47.50, 41.46
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  403G   54G  89% /
edit-4jhi: deferred retained BASE reopen text=yes modified=yes set-aside=no target=yes
edit-4jhi: target=unchanged acquisition=MMAP checkpoint relaunch preserved BASE edits and TARGET
edit-4jhi: deferred retained BASE reopen text=yes modified=yes set-aside=no target=yes
edit-4jhi: target=rewritten acquisition=COPY checkpoint relaunch preserved BASE edits and TARGET
exit=0
Not charging
 12:22:11 up 6 days, 17:10,  1 user,  load average: 27.86, 47.50, 41.46
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  403G   54G  89% /
```

## Verification commands and exit codes

Release compiler: GCC 13.3.0; sanitizer compiler: clang 18.1.3, as reported by
the installed tools. Project compilation used C11 with `-Wall -Wextra -Werror
-Wshadow -Wconversion`; sanitizer targets use ASan and UBSan.

```sh
make -j3 all
# exit=0
make -j3 build/san/tests/editor_session_test build/san/tests/editor_checkpoint_test build/san/tests/dataloss_test build/san/tests/journal_test
# exit=0
```

Each full suite ran through the wrapper below. It creates a fresh
`XDG_RUNTIME_DIR` using `mktemp -d`, traps its removal, enables leak detection,
uses the supplied lock timeout, and stamps power/uptime/disk before and after:

```sh
DISPLAY=:97 sh build/verification-edit-4jhi/run.sh build/san/tests/editor_session_test
# exit=0
DISPLAY=:97 sh build/verification-edit-4jhi/run.sh build/san/tests/editor_checkpoint_test
# exit=0
DISPLAY=:97 sh build/verification-edit-4jhi/run.sh build/san/tests/dataloss_test
# exit=0
DISPLAY=:97 sh build/verification-edit-4jhi/run.sh build/san/tests/journal_test
# exit=0
DISPLAY=:97 EDITOR_ALLOC_ONLY=null sh build/verification-edit-4jhi/run.sh build/tests/editor_test
# exit=0
DISPLAY=:97 EDITOR_ALLOC_ONLY=raster sh build/verification-edit-4jhi/run.sh build/tests/editor_test
# exit=0
```

No sanitizer or leak errors occurred in these completed verification runs.
The initial sandboxed red run encountered LeakSanitizer's process-inspection
restriction. Completed red/green and full verification runs used approved
execution outside that restriction. The initial display-lane wait was
interrupted and the permitted free lane was used; its socket was reachable.

Release allocator guard output:

```text
Charging
 12:25:17 up 6 days, 17:13,  1 user,  load average: 56.40, 52.46, 44.43
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  403G   53G  89% /
editor_test: null 10000 keys mallocs=0 guard=active
exit=0
Charging
 12:25:18 up 6 days, 17:13,  1 user,  load average: 56.40, 52.46, 44.43
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  403G   53G  89% /
```
```text
Charging
 12:25:18 up 6 days, 17:13,  1 user,  load average: 56.40, 52.46, 44.43
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  403G   53G  89% /
editor_test: raster 10000 keys mallocs=0 guard=active
exit=0
Charging
 12:25:24 up 6 days, 17:13,  1 user,  load average: 56.65, 52.64, 44.58
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  403G   53G  89% /
```

## Sparse-fixture cleanup and disk-space captures

The new fixtures are sparse (`ftruncate` plus the literal prefix), and the
outer wrapper removes their entire directory after any body return. Full
suites include the existing sparse GB fixtures. The final cleanup check
printed no matching fixture directories:

```sh
find build -maxdepth 1 -type d \( -name 'editor-4jhi-*' -o -name 'editor-601-*' -o -name 'editor-fm4-*' -o -name 'editor-qct-*' \) -print
# exit=0; no output
```

Exact `df -h /` output before and after each full sanitizer suite follows.
The release build ran concurrently, so disk use also includes build output.
The focused red/green captures above include their own before/after output.

`editor_session_test` — before:

```text
Charging
 12:22:26 up 6 days, 17:10,  1 user,  load average: 26.89, 46.32, 41.17
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  403G   54G  89% /
```

After:

```text
exit=0
Charging
 12:23:17 up 6 days, 17:11,  1 user,  load average: 44.03, 47.75, 41.91
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  403G   54G  89% /
```

`editor_checkpoint_test` — before:

```text
Charging
 12:23:17 up 6 days, 17:11,  1 user,  load average: 44.03, 47.75, 41.91
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  403G   54G  89% /
```

After:

```text
exit=0
Not charging
 12:23:36 up 6 days, 17:11,  1 user,  load average: 54.71, 49.94, 42.76
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  403G   53G  89% /
```

`dataloss_test` — before:

```text
Not charging
 12:23:36 up 6 days, 17:11,  1 user,  load average: 54.71, 49.94, 42.76
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  403G   53G  89% /
```

After:

```text
exit=0
Charging
 12:24:07 up 6 days, 17:12,  1 user,  load average: 59.20, 51.49, 43.50
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  403G   53G  89% /
```

`journal_test` — before:

```text
Charging
 12:24:07 up 6 days, 17:12,  1 user,  load average: 59.20, 51.49, 43.50
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  403G   53G  89% /
```

After:

```text
exit=0
Charging
 12:24:36 up 6 days, 17:12,  1 user,  load average: 59.43, 52.30, 44.03
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  403G   53G  89% /
```

## What remains / outside scope

No further changes were made outside F6. The independent source/target
binding is covered for mapped length-changing replay and COPY same-length
replay, and both survive checkpoint relaunch.

Found while extending the regression: COPY-mode pending adoption retains
`b->index` sized to `file_size`, while lazy replay can change the tree length.
`editor_adopt_open` recreates the index for MMAP only;
`lineidx_bind_snapshot` rejects a COPY snapshot whose edited length differs
from that index. The attempted COPY insertion therefore fell back to an
empty ordinary buffer with notice "large session journal could not be
restored; journal retained". Its source stayed discoverable, and it was not
set aside. The debugger located the failure at adoption in `install_all`;
the length rejection is visible in `src/lineidx/lineidx.c` and the MMAP-only
recreation in `src/editor/buffers.c`. This is not the retained-inode mismatch,
and was reported without a fix as instructed. Length-changing COPY lazy
recovery remains to be handled separately; the COPY F6 regression keeps its
net length unchanged to isolate this bead.

Retained artifact lifetime/cleanup policy is unchanged. A retained generation
still referenced by the installed checkpoint must remain available; this bead
does not sweep it. No extra review pass, broad refactor, or unrelated fix was
performed. Forbidden tracking commands and documentation edits were not used.
