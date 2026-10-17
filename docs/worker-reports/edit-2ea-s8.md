# edit-2ea — s8 worker report

`editor_paint_prepare` now makes reversed decoration endpoints an empty
half-open range before subtracting them. The regression checks direct paint
preparation, the editor error latch, and submission of a blank text frame
including its gutter. `EDITOR_IDLE_NS` already carries the edit-dz0 derivation
beside its definition; the header is unchanged.

## Files touched

- `src/editor/paint.c`: clamp the upper endpoint to the lower endpoint when reversed.
- `tests/editor_test.c`: all-void regression, focused selector, and default-suite entry.
- `docs/decisions/edit-2ea.md`: range and fixture decisions; idle-constant check.
- `docs/worker-reports/edit-2ea-s8.md`: this report.

Generated build outputs and raw evidence are under `build/`, with bead logs
and original-file copies in `build/edit-2ea/`.

## Red run — test first, production fix absent

Raw observations below are (M)[AC], with literal BAT0 and uptime stamps.
(P) `EDITOR_ERR_ARG = -100`, `EDITOR_OK = 0`, and `EDITOR_MORE = 1` are the
return-code declarations in `src/editor/editor.h`. Test/process exit status
zero means success; the test assertion returns one on failure. These are
status codes, not budgets. The fixture's geometry and prefix offset are
derived beside the test from the existing trailing-paint fixture and string
length; no new scan or time bound was introduced.

```text
$ sh tools/with_display_lock.sh build/tests/editor_test --paint-all-void
Not charging
 09:43:48 up 6 days, 14:31,  1 user,  load average: 39.83, 47.13, 51.57
editor_test:229: FAIL prepared == 0
all-void paint: prepare=-100 step=-100 latched=-100
exit=1
```

## Green run — clamp applied

(M)[AC], same focused command and fixture:

```text
$ sh tools/with_display_lock.sh build/tests/editor_test --paint-all-void
Not charging
 09:50:38 up 6 days, 14:38,  1 user,  load average: 42.51, 44.94, 49.04
all-void paint: prepare=0 step=0 latched=0
all-void paint: blank frame submitted passed
exit=0
```

## Verification

Build parallelism, the display identifier, and the enabled leak-detection
switch follow the brief. Each display invocation used `DISPLAY=:99`,
`ASAN_OPTIONS=detect_leaks=1`,
and a fresh `XDG_RUNTIME_DIR` created with `mktemp -d` and removed on exit.
Successful lock acquisition uses blocking `flock` on the existing
`/tmp/edit-xvfb-99.lock`, passing that open descriptor as
`EDIT_DISPLAY_LOCK_FD` to the unchanged `tools/with_display_lock.sh`.
Execution needed expanded permission because the sandbox blocks the Xvfb
Unix socket. Power/load stamps are taken after acquiring the display lock.

(P) Exit codes below follow the process conventions above; the header
comparison's zero status means identical contents.

| Command | Exit |
| --- | --- |
| `make -j3 all` | 0 |
| `make -j3 build/san/tests/editor_test` | 0 |
| `sh tools/with_display_lock.sh build/tests/editor_test --paint-all-void` (red) | 1 |
| `sh tools/with_display_lock.sh build/tests/editor_test --paint-all-void` (green) | 0 |
| `sh tools/with_display_lock.sh build/san/tests/editor_test` | 0 |
| `sh tools/with_display_lock.sh build/tests/editor_test` (release malloc guard) | 0 |
| `cmp -s src/editor/editor.h build/edit-2ea/editor.h.before` | 0 |

Build logs: `build/edit-2ea/green-all.log` and
`build/edit-2ea/green-san-build.log`. Runtime logs:
`build/edit-2ea/green-san-editor.log` and
`build/edit-2ea/green-release-editor.log`.

## Full sanitizer run

(M)[AC], raw environment, regression, completion, and disk stamps:

```text
Not charging
 09:50:38 up 6 days, 14:38,  1 user,  load average: 42.51, 44.94, 49.04
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  386G   70G  85% /
all-void paint: prepare=0 step=0 latched=0
all-void paint: blank frame submitted passed
editor_test: all passed
Not charging
 09:56:45 up 6 days, 14:44,  1 user,  load average: 35.73, 40.38, 45.83
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  386G   70G  85% /
exit=0
```

No ASan, UBSan, or LeakSanitizer diagnostics matched the log scan:

```sh
rg -n 'runtime error:|ERROR: (AddressSanitizer|LeakSanitizer)|AddressSanitizer:DEADLYSIGNAL' build/edit-2ea/green-san-editor.log
```

(P) That scan exited one, the documented `rg` status for no matching lines.

## Full release run — malloc guard

(M)[AC], raw observations from the existing suite fixtures. Release allocation
guards were active and reported zero allocations (P: the typing-path law
requires zero). Disk stamps precede and follow the full suite.

```text
Not charging
 09:56:46 up 6 days, 14:44,  1 user,  load average: 35.73, 40.38, 45.83
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  386G   70G  85% /
all-void paint: prepare=0 step=0 latched=0
all-void paint: blank frame submitted passed
clipboard malloc guard: 0 allocations (active); expected zero by law
review 21: native dequeue-to-translation mallocs=0; mutation-to-submit=0; guard=active (M)[AC]
P1-1 section 6: inactive exact counts/jump, bulk cancellation/retry, mallocs=0 guard=active passed (M)[AC]
editor_test: requested=gl actual=cpu-raster 10000 keys mallocs=0 guard=active
editor_test: requested=raster actual=cpu-raster 10000 keys mallocs=0 guard=active
editor_test: null 10000 keys mallocs=0 guard=active
editor_test: null hl-on 10000 keys mallocs=0 guard=active
editor_test: raster 10000 keys mallocs=0 guard=active
editor_test: all passed
Not charging
 09:59:00 up 6 days, 14:47,  1 user,  load average: 31.37, 37.18, 43.92
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  386G   70G  85% /
exit=0
```

## What remains

No work remains within edit-2ea.

## Outside the bead's scope

The default sandbox's Xvfb socket probe returned `EPERM`. Initial polling-lock
attempts timed out before executing the test; their exit status was one (P:
`with_display_lock.sh` timeout return). The raw logs are
`build/edit-2ea/red-lock-timeout.log`, `build/edit-2ea/red.log`, and
`build/edit-2ea/red-expanded-lock-timeout.log`. Expanded execution and blocking
acquisition of the same shared lock allowed the actual red/green runs above.
No additional product issue was found while doing this bead.
