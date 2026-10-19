# edit-nlz — worker report

Implemented the shared selector helper and static tables in the requested test
mains. Default run lists, selector names, argument-count policies and callback
exit codes are preserved. The helper contract is red then green. Release build
and allocation guards pass. Verification also found an existing CLI failure and
an intermittent dataloss lock assertion, detailed below; this is not a clean
aggregate suite run.

## Files touched

- `tests/editor_test.c`: selector table and adapters; retain headless and prefix modes.
- `tests/cli_test.c`: selector table and signal adapters.
- `tests/editor_session_test.c`: selector table and grouped/parameterized adapters.
- `tests/editor_large_test.c`: environment selector tables at the original selection points.
- `tests/dataloss_test.c`: selector table and adapters; retain probe mode.
- `tests/select.h`: exact-name and environment-presence lookup, array-derived lengths.
- `tests/select_test.c`: labeled helper contracts, including exit-code preservation.
- `docs/decisions/edit-nlz.md`: design choices and special-mode preservation.
- `docs/worker-reports/edit-nlz-s9.md`: this report.

Generated binaries, saved original test sources, commands and complete logs are
under `build/`, with bead evidence under `build/edit-nlz/`. Production source and
the prohibited documents were not edited. Neither `bd` nor `git` was run.

## Evidence conventions

(P) Argument positions, array bounds and return values follow the existing C
interfaces. Existing fixture arguments are retained. The job limit, display,
lock timeout and toolchain settings come directly from the binding brief (E,
coordinator-supplied constraints), with no added budgets. Captured outputs below,
including exit codes, line references, fixture key counts and disk usage, are
(M)[AC]. Power was `Full`; `/sys/class/power_supply/AC/online` returned `1`
(P: mains-online flag). Runtime logs contain the requested battery-status and
`uptime` samples before and after each invocation. No elapsed-time estimates
were added.

## Red run

Command: `make -j3 build/san/tests/select_test`; exit `2` (M)[AC]. This is the
new contract test failing to compile before its helper exists; the bead changes
no production behavior. Complete captured red output:

```text
clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -D_GNU_SOURCE -pthread -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -Isrc -Ivendor/tree-sitter/lib/include -DEDIT_HAVE_WAYLAND -DEDIT_HAVE_WAYLAND_SERVER -include tests/display_guard.h -MMD -MP -c tests/select_test.c -o build/san/tests/select_test.o
clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -D_GNU_SOURCE -pthread -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -Isrc -Ivendor/tree-sitter/lib/include -DEDIT_HAVE_WAYLAND -DEDIT_HAVE_WAYLAND_SERVER  -MMD -MP -c src/base/base.c -o build/san/src/base/base.o
clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -D_GNU_SOURCE -pthread -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -Isrc -Ivendor/tree-sitter/lib/include -DEDIT_HAVE_WAYLAND -DEDIT_HAVE_WAYLAND_SERVER  -MMD -MP -c src/base/malloc_guard.c -o build/san/src/base/malloc_guard.o
tests/select_test.c:2:10: fatal error: 'select.h' file not found
    2 | #include "select.h"
      |          ^~~~~~~~~~
1 error generated.
make: *** [Makefile:172: build/san/tests/select_test.o] Error 1
make: *** Waiting for unfinished jobs....
```

## Green run

The sanitizer build command below exits `0` (M)[AC]. The helper then passes with
leak detection enabled, including non-boolean callback return preservation:

```text
command: build/san/tests/select_test
environment: ASAN_OPTIONS=detect_leaks=1 DISPLAY=:99 EDIT_DISPLAY_LOCK_TIMEOUT=3600 XDG_RUNTIME_DIR=$(mktemp -d) {}
Full
 05:43:17 up  4:08,  1 user,  load average: 5.97, 7.12, 7.93
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  391G   65G  86% /
select_test: exact names, missing/empty lookup, first match and exit codes passed (P)
select_test: unset/empty environment selectors and table precedence passed (P)
Full
 05:43:17 up  4:08,  1 user,  load average: 5.97, 7.12, 7.93
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  391G   65G  86% /
fixture cleanup: []
exit=0
```

## Verification commands and exit codes

All sanitizer and release test invocations ran outside the sandbox. Display
suites used the following form, including the supplied timeout (E, brief) and
leak setting (P, LeakSanitizer enable flag):

```sh
XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 EDIT_DISPLAY_LOCK_TIMEOUT=3600 \
  ASAN_OPTIONS=detect_leaks=1 sh tools/with_display_lock.sh <binary> [selector]
```

The runner removes its runtime directory in `finally`. It also removes any new
fixture path under this worktree's build root on every exit. Dataloss's failed
initial run left a fixture root that the runner removed. Existing corpus files
were not created for this bead and were left in place.

Build and source-preservation commands (results M[AC]):

| Command | Exit | Evidence |
|---|---|---|
| `make -j3 all` | `0` | `build/edit-nlz/make-all-final.log`; all release test binaries built with the required project warnings |
| `python3 build/edit-nlz/verify_sources.py` | `0` | selector name sets and default source suffixes match; large-suite selector order/default assertions/final fixture match |
| `python3 build/edit-nlz/run_checks.py san` | `1` | CLI and initial dataloss assertions below |
| `python3 build/edit-nlz/run_checks.py release` | `0` | active null and raster allocation guards |
| `python3 build/edit-nlz/run_checks.py baseline` | `1` | original CLI fails; original dataloss passes |
| `python3 build/edit-nlz/run_checks.py dataloss-recheck` | `0` | recorded repeat of the modified default suite |

Sanitizer build, exit `0` (M)[AC], using the same targets and configuration as
`make check` (`build/edit-nlz/make-san.log`):

```sh
make -j3 build/san/tests/select_test build/san/tests/editor_test \
  build/san/tests/cli_test build/san/tests/editor_session_test \
  build/san/tests/editor_large_test build/san/tests/dataloss_test \
  build/san/tests/editor_scroll_test build/san/tests/editor_hot_exit_test \
  build/san/tests/raster_test
```

Per-invocation results (M)[AC]; an empty selector means the unmodified default
run. Every row has a corresponding `build/edit-nlz/<label>.log`:

| Label | Binary | Selector / extra environment | Exit |
|---|---|---|---|
| select-green | `build/san/tests/select_test` | `(default)` | `0` |
| editor-selected | `build/san/tests/editor_test` | `--paint-all-void` | `0` |
| editor-default | `build/san/tests/editor_test` | `(default)` | `0` |
| cli-selected | `build/san/tests/cli_test` | `--display-policy` | `0` |
| cli-default | `build/san/tests/cli_test` | `(default)` | `1` |
| session-selected | `build/san/tests/editor_session_test` | `--explicit-changed-base` | `0` |
| session-default | `build/san/tests/editor_session_test` | `(default)` | `0` |
| large-selected | `build/san/tests/editor_large_test` | `EDIT_JF9_VIEW=''` | `0` |
| large-default | `build/san/tests/editor_large_test` | `(default)` | `0` |
| dataloss-selected | `build/san/tests/dataloss_test` | `--shared-save-dirty` | `0` |
| dataloss-default | `build/san/tests/dataloss_test` | `(default)` | `1` |
| scroll-default | `build/san/tests/editor_scroll_test` | `(default)` | `0` |
| hot-exit-default | `build/san/tests/editor_hot_exit_test` | `(default)` | `0` |
| raster-default | `build/san/tests/raster_test` | `(default)` | `0` |
| cli-baseline-default | `build/edit-nlz/cli-baseline` | `(default)` | `1` |
| dataloss-baseline-default | `build/edit-nlz/dataloss-baseline` | `(default)` | `0` |
| dataloss-default-recheck | `build/san/tests/dataloss_test` | `(default)` | `0` |
| release-malloc-null | `build/tests/editor_test` | `EDITOR_ALLOC_ONLY='null'` | `0` |
| release-malloc-raster | `build/tests/editor_test` | `EDITOR_ALLOC_ONLY='raster'` | `0` |

Release allocation-guard output (M)[AC]; the key count is the existing fixture's
loop count, and the zero allocation target is the binding typing-path law:

```text
command: sh tools/with_display_lock.sh build/tests/editor_test
environment: ASAN_OPTIONS=detect_leaks=1 DISPLAY=:99 EDIT_DISPLAY_LOCK_TIMEOUT=3600 XDG_RUNTIME_DIR=$(mktemp -d) {'EDITOR_ALLOC_ONLY': 'null'}
Full
 05:41:47 up  4:07,  1 user,  load average: 6.92, 7.68, 8.18
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  391G   65G  86% /
editor_test: null 10000 keys mallocs=0 guard=active
Full
 05:41:48 up  4:07,  1 user,  load average: 6.92, 7.68, 8.18
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  391G   65G  86% /
fixture cleanup: []
exit=0
command: sh tools/with_display_lock.sh build/tests/editor_test
environment: ASAN_OPTIONS=detect_leaks=1 DISPLAY=:99 EDIT_DISPLAY_LOCK_TIMEOUT=3600 XDG_RUNTIME_DIR=$(mktemp -d) {'EDITOR_ALLOC_ONLY': 'raster'}
Full
 05:41:48 up  4:07,  1 user,  load average: 6.92, 7.68, 8.18
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  391G   65G  86% /
editor_test: raster 10000 keys mallocs=0 guard=active
Full
 05:41:53 up  4:07,  1 user,  load average: 6.68, 7.62, 8.16
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  391G   65G  86% /
fixture cleanup: []
exit=0
```

## Outside the bead / what remains

CLI default fails the hot-exit snapshot comparison. Compiling the saved original
`tests/cli_test.c` against the same sanitizer library and production executable
reproduces the same failure. The original-source build reused the recorded
sanitizer compile/link arguments, reading the saved source on stdin with
`-iquote tests` so includes resolve inside this worktree; compile and link each
exit `0` (M)[AC]. Exact commands are in `cli-baseline-build.log`. The forced
startup-timeout failure earlier in each CLI log is an expected control case;
the actual failing comparison is:

```text
cli-default:
cli_test:499: FAIL after.count == before.count && after.active == before.active && after.len == before.len
cli_test:567: FAIL hot_exit_cli(source_path, dir, stop_signal) == 0
cli-baseline-default:
cli_test:498: FAIL after.count == before.count && after.active == before.active && after.len == before.len
cli_test:566: FAIL hot_exit_cli(source_path, dir, stop_signal) == 0
```

Dataloss's initial default run fails acquiring the journal-directory lock in
`ivv_choice(false)`. The original source's complete default run passes against
the same sanitizer library; its compile and link each exit `0` (M)[AC], with
commands in `dataloss-baseline-build.log`. Initial failure:

```text
dataloss_test:1122: FAIL guard >= 0 && flock(guard, LOCK_EX | LOCK_NB) == 0
dataloss_test:1629: FAIL ivv_choice(false) == 0
```

The modified dataloss default recheck also passes (M)[AC], so the lock
assertion is intermittent in these runs. Its cause remains unproven; default
source preservation is verified.

Implementation scope is complete. The coordinator still needs to triage the
CLI failure and the intermittent dataloss lock assertion before claiming clean
aggregate verification. These verification failures were recorded
without changes outside the selector scope. No additional review pass was performed.

## Sparse-fixture disk evidence

The existing session and large-file constructors create sparse large fixtures;
their existing cleanup and the runner's exit cleanup removed worktree fixture
paths. The following are the actual `cat /sys/class/power_supply/BAT0/status`,
`uptime`, and `df -h /` outputs before and after the relevant default runs
(M)[AC], including the failing run and its recheck:

session-default — before, then after cleanup:

```text
Full
 05:46:47 up  4:12,  1 user,  load average: 9.52, 7.81, 8.02
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  392G   64G  86% /
Full
 05:47:22 up  4:12,  1 user,  load average: 9.34, 7.98, 8.07
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  392G   64G  86% /
```

large-default — before, then after cleanup:

```text
Full
 05:47:22 up  4:12,  1 user,  load average: 9.34, 7.98, 8.07
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  392G   64G  86% /
Full
 05:47:31 up  4:12,  1 user,  load average: 8.14, 7.77, 8.00
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  392G   64G  86% /
```

dataloss-default — before, then after cleanup:

```text
Full
 05:47:33 up  4:12,  1 user,  load average: 8.14, 7.77, 8.00
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  392G   64G  86% /
Full
 05:47:34 up  4:12,  1 user,  load average: 8.14, 7.77, 8.00
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  392G   64G  86% /
```

dataloss-baseline-default — before, then after cleanup:

```text
Full
 05:50:38 up  4:15,  1 user,  load average: 7.03, 6.72, 7.49
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  393G   63G  87% /
Full
 05:51:21 up  4:16,  1 user,  load average: 9.76, 7.50, 7.73
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  393G   63G  87% /
```

dataloss-default-recheck — before, then after cleanup:

```text
Full
 05:52:03 up  4:17,  1 user,  load average: 8.30, 7.44, 7.70
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  393G   63G  87% /
Full
 05:52:43 up  4:17,  1 user,  load average: 8.28, 7.53, 7.72
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  393G   63G  87% /
```
