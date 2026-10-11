# edit-40t — benchmark fault checks and remaining integration scope

Partial delivery of the assigned bead. The benchmark self-check defect is
fixed; startup bootstrap, blink integration and paired identity remain outside
the permitted edit boundary. No `bd` or `git` command was run. All work stayed
in this worktree. No typing-path code or allocation behavior changed.

## Changes

- `bench/gl_bench.c`: emit `INJECTED stage=...` at the actual fault site;
  route injected acquisition failures directly through the existing owner
  cleanup. Driver preparation and initialization errors fail unless the driver
  specifically returns `RENDER_ERR_UNSUPPORTED`. Resource errors and initializer
  timeouts can no longer masquerade as missing native infrastructure.
- `tests/gl_test.c`: verify that the requested injection was actually reached,
  reject an infrastructure skip for the display-independent arena fault,
  require acquisition injections to avoid assertion-failure diagnostics, and
  check worker cleanup even when native prerequisites prevent an injection.
- `tests/gl_bench_test.c`: script native admission while using real worker
  acquisition and benchmark cleanup. Exercise preparation and initialization
  errors, unsupported native initialization, and the prepared-worker fault
  without opening a display.
- Documentation: this report, `src/gl/STATUS.md`,
  `docs/decisions/edit-40t.md`, `HANDOFF.md`, and the current worklog.

The originally reported `FAIL !gl_bench_fault("arena")` and
`FAIL !gl_bench_fault("prepare")` were expected negative controls printed as
assertion failures. They do not demonstrate missing EGL or a cleanup leak.
The preparation fault follows X connection/worker acquisition and precedes
EGL initialization. Here the X socket is denied, so the native preparation
fault is explicitly unexercised; the headless transport exercises prepared
worker cleanup. A separate real defect was the blanket conversion of all
driver failures into successful infrastructure skips.

The previous session report also records native initialization returning
`RENDER_ERR_INIT`, labelled as unavailable EGL. That stage cannot be reproduced
in this sandbox, and absence of EGL is not established by that return code.
The benchmark now fails that result, making a recurring resource/init error
visible to the coordinator instead of accepting it as an environmental skip.

## Red before implementation

GCC release runs, with C11 and the repository warning flags. Power was
`Charging` (M)[AC], read from `/sys/class/power_supply/BAT0/status` before
execution. Exit codes derive from the shell's `$?`; failure is `EXIT_FAILURE`,
success is `EXIT_SUCCESS`, and the native skip code is the existing benchmark
convention. Render result codes below derive from `src/render/render.h`.
Line numbers are compiler diagnostic locations, not measured quantities.

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 build/tests/gl_test --review p2-5
```

```text
gl_bench:463: FAIL !gl_bench_fault("arena")
gl_test:1356: FAIL rc==1 && injected
exit=1
```

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 build/tests/gl_bench_test
```

```text
gl_bench_test:45: FAIL rc==EXIT_FAILURE
BENCH name=egl_init_15px status=SKIP reason=X11_unavailable result=-9 power=[AC]
gl_bench_test: stage=prepare-error result=2
exit=1
```

The pixel suffix is the existing baked-font fixture passed to `gl_bench_run`;
it is not a new performance budget. Raw runs are in
`build/edit-40t-evidence/red-release.log` and `classification-red.log`.

## Green

GCC release, same power source/status and flags as red:

```text
gl_bench: INJECTED stage=self-overlap
gl_bench: INJECTED stage=arena
gl_test: section 5 stage=arena injected cleanup PASS
gl_test: section 5 stage=prepare SKIP: native prerequisite unavailable; fault not reached
gl_test: P2-1 section 5 PASS (arena/driver acquisition, active contention failure cleanup; native stages conditional)
exit=0
gl_bench: FAIL driver_prepare result=-9
gl_bench: FAIL driver_init result=-9
gl_bench: INJECTED stage=prepare
gl_bench_test: stage=prepare-error result=1
gl_bench_test: stage=init-error result=1
gl_bench_test: PASS (driver errors fail, unsupported native skips, prepared worker cleanup)
exit=0
```

The driver `FAIL` diagnostics in the headless test are scripted error negative
controls; their failure exits are required by the assertions. Acquisition
injections themselves use `INJECTED`, and cannot be inferred from a generic
failure return. The section labels refer to the supplied review.

Raw green runs: `build/edit-40t-evidence/green-release.log` and
`classification-green-release.log`.

## Verification

`make all` passed under GCC with the repository warning flags. Build parallelism
was the available CPU count returned by `nproc`, rather than a new budget.
Raw compiler output: `build/edit-40t-evidence/all.log`.

The same regression commands shown above passed under clang ASan/UBSan with
`ASAN_OPTIONS=detect_leaks=0` (M)[AC], derived from their command exits and BAT0
`Charging`. Green sanitizer excerpts:

```text
gl_bench: INJECTED stage=arena
gl_test: section 5 stage=arena injected cleanup PASS
gl_test: section 5 stage=prepare SKIP: native prerequisite unavailable; fault not reached
gl_test: P2-1 section 5 PASS (arena/driver acquisition, active contention failure cleanup; native stages conditional)
exit=0
gl_bench_test: stage=prepare-error result=1
gl_bench_test: stage=init-error result=1
gl_bench_test: PASS (driver errors fail, unsupported native skips, prepared worker cleanup)
exit=0
```

Raw sanitizer logs: `green-sanitizer.log`,
`classification-green-sanitizer.log`, and `gl-pure-reviews.log` in the evidence
directory. The latter exercises all existing fixed review lanes that require
no native display, plus the deterministic completion/descriptor/trace lanes.

The required whole-tree command was run:

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 make check
```

```text
== build/san/tests/base_test
LeakSanitizer has encountered a fatal error.
HINT: LeakSanitizer does not work under ptrace (strace, gdb, etc)
make: *** [Makefile:105: check] Error 1
```

LeakSanitizer cannot operate under this sandbox's ptrace instrumentation. It
was disabled for subsequent runs; AddressSanitizer and UndefinedBehaviorSanitizer
remain enabled. A coordinator leak-enabled run is still required.

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make check
```

```text
base_test: PASS
== build/san/tests/cli_test
cli_test:215: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
make: *** [Makefile:105: check] Error 1
```

The sandbox denies local UNIX socket binding too: a direct `socket(AF_UNIX,
SOCK_STREAM)` bind to a path under this worktree returned `Operation not
permitted`, with the errno supplied by the operating system. The CLI failure
is IPC admission before window creation, rather than an exercised display
test. Raw command logs: `check-leaks.log` and `check.log`.

Since `make check` stops at the first failure, all sanitizer suite binaries were
also run individually with leak detection disabled. Results (M)[AC]: **64
executed, 47 successful, 17 failed**, derived respectively from
`len(suite-results.json)`, the count of successful process exits, and the count
of unsuccessful exits. The JSON contains one result for each binary matched by
`build/san/tests/*_test`; the matching source glob is in the Makefile. BAT0
readings were `Charging` and `Not charging`, both classified as AC by the
repository power helper. Aggregate output is `all-suites.log`; individual logs
use each suite's name in the evidence directory.

All default GL suites (`gl_bench_test`, `gl_gate_test`, `gl_test`) succeeded.
The default GL suite concludes:

```text
gl_test: PASS (pure snapshot/completion/trace checks; native diagnostics SKIP)
```

Display-dependent failures, to rerun on the coordinator's authorized Xvfb
fixture:

| Suite | Observed native failure |
| --- | --- |
| `editor_close_test` | Headless close passes; native raster editor open fails. |
| `file_kill_test` | Benchmark viewport `plat_init` fails, including queued child fixtures. |
| `raster_test` | `plat_init` fails in the native lifecycle fixture; pure review checks preceding it pass. |
| `refproto_test` | Display connection fails. |
| `refwin_test` | Display connection fails. |
| `refwin_timeout_test` | Cannot reach its injected input-stage timeout because display connection fails first. |
| `x11_clip_review_test` | Connection/fixture initialization fails. |
| `x11_clip_test` | `plat_init` fails after its private Xvfb startup attempt. |
| `x11_identity_test` | `plat_init` fails. |
| `x11_live_test` | Protocol-error fixtures cannot get their required native startup. |
| `x11_order_test` | Native fixtures cannot initialize. |
| `x11_stall_test` | Live display initialization fails. |

Additional socket-dependent failures are `cli_test`, `editor_p4_test` and
`ipc_test`. They fail at IPC server initialization. `ipc_test` explicitly
prints `Operation not permitted`, confirming the UNIX socket restriction.
These are not successful display tests or successful non-display acceptance.

Outside-scope failures and serial follow-up evidence:

- `editor_large_test` initially hit the existing cooperative-job fixture's
  deadline. Its serial rerun passed that case and the intervening headless
  cases, then failed `mapped_startup_ipc()` at denied server initialization.
  See `editor-large-rerun.log`.
- `editor_test` initially failed headless recovery initialization after earlier
  fixtures. The isolated `--ck4-keep` lane succeeded. Its full serial rerun
  passed the COPY/mapped Keep/Reload recovery cases, then failed native window
  fixtures and a launcher-wait assertion: `reload launcher result=4`, whose
  value is `IPC_IO` from the existing IPC result enum. The launcher-wait
  failure is not diagnosed or claimed to be caused by GL or the display.
  See `editor-keep-rerun.log` and `editor-rerun.log`. No editor/IPC fix was made.

All other suite binaries in the complete individual run returned success.
The required full check is **not green**: socket/display access, leak-enabled
verification, and the unresolved launcher-wait failure need coordinator
verification. The replay CLI shell contracts pass with leak detection disabled.

The GL-free surrogate and production index/find/durable-save benchmark
self-checks passed; their raw logs are `bulk.log` and `integrated.log` in the
evidence directory. The normal release `gl_bench --quick` ran and explicitly
skipped native rows because X is unavailable (`RENDER_ERR_UNSUPPORTED`), using
the existing native skip exit convention. Its power/load stamp and raw output
are in `bench.log`. No native latency or performance acceptance is claimed.

## What remains and observations outside scope

The supplied review's startup, blink and paired-identity integration findings
(P2-1 sections 13, 14 and 20) remain. The prior report
`docs/worker-reports/edit-40t-s9.md` contains their failing regression evidence.
Their fixes require `src/editor/open.c` and editor lifecycle/input polling,
plus `tools/keyinject.c`, `tools/refwin_pairs.py` and `tests/refwin_test.c`.
This worker is restricted to GL, the specifically named editor backend file,
the GL benchmark and GL tests. Factory selection alone cannot safely implement
the required raster bootstrap/handoff, remove duplicate editor polling, or
teach the paired joiner an explicit native serial association. Those files
were not changed, and the optional review lanes were not weakened.

The blink and paired-identity optional lanes were rerun under ASan/UBSan and
remain red:

```text
gl_test:1283: FAIL no_timer
exit=1
ValueError: pair 1: editor Present serial has no unique matching key frame
gl_test:1110: FAIL WIFEXITED(status) && WEXITSTATUS(status)==0
exit=1
```

These diagnostic positions come from the compiled tests; the pair identifier
comes from the existing mismatch fixture, not a measurement. Raw logs are
`remaining-blink.log` and `remaining-pairs.log`. The startup lane requires a
display and retains the prior report's held-initializer evidence.

The coordinator must rerun display suites on its authorized Xvfb fixture
(:99, supplied by the task), then run native EGL/Present acceptance on a fixture
that supports it. The next Astra comparison drive must verify the resulting
build before handover. This worker has no access to the main-repo drive
artifacts or an authorized native display and does not claim that verification.

No unrelated code was fixed.
