# edit-hltk-s12 worker report

Verdict: verification in progress; final full-check result is recorded below.

## Scope and changes

All source reads, edits and builds were in this worktree, except the binding
rules and brief read from the main checkout as instructed. No packages were
installed, no `bd` was run, and git was used only for status and diffs.

- `tests/journal_retire_test.c`: tolerate missing BAT0, retain `unknown` status
  and the AC stamp, and append `(status file absent)` when opening reports
  `ENOENT`. Other measurement assertions and test expectations are retained.
- `tests/x11_xvfb.h`: retain automatic `-displayfd` startup on writable socket
  directories; on an unwritable directory, bind-probe for a free positive
  abstract display and pass it explicitly, retaining `-displayfd` readiness.
- `tools/refwin_protocol.h`: every failed-open branch diagnoses the failure,
  then releases the XCB connection and any loaded library through the existing
  close routine.
- `tests/refproto_test.c`: add a fixture-local missing-runtime regression using
  the existing compile-time seam pattern. It asserts failed open leaves no
  connection/library; existing clock/protocol assertions are unchanged.
- `docs/decisions/edit-hltk.md`: record the startup and cleanup choices.
- This report.

## Environment and evidence convention

All measured results here are (M)[AC, desktop Spiral]. BAT0/status is absent.
Counts and exit codes below are observations from the named command logs,
not budgets. Display names and screen dimensions are command inputs, not
measurements. No performance claim is made.

Initial `uptime`:

```text
08:14:11 up 30 min,  1 user,  load average: 0.16, 0.11, 0.09
```

Focused green `uptime`:

```text
08:19:35 up 36 min,  1 user,  load average: 0.36, 0.75, 0.49
```

The compiler commands used clang for ASan/UBSan and gcc for release; version
output identified Ubuntu clang 18.1.3 and gcc 13.3.0 (M: `--version`). Leak
detection remained enabled. The sandbox prohibits abstract socket binding
(`PermissionError: Operation not permitted`), so display execution and manual
diagnosis ran with that restriction lifted through the execution tool. There
was no approval-review rejection and no ptrace/LeakSanitizer limitation on
those runs. `ASAN_OPTIONS=detect_leaks=0` was never used.

Evidence logs and scratch probes are under `build/edit-hltk-evidence/` in this
worktree. They are build artifacts, not additional source changes.

## Red runs, before implementation

Build command (exit 0):

```sh
make -j64 build/san/tests/journal_retire_test build/san/tests/x11_live_test build/san/tests/x11_order_test build/san/tests/refproto_test
```

The original suites were run before any edits. Relevant output:

```text
$ build/san/tests/journal_retire_test
journal_retire_test:298 FAIL f
journal_retire_test:617 FAIL measurement()==0
exit=1

$ DISPLAY=:99 tools/with_display_lock.sh build/san/tests/x11_live_test
Fatal server error:
(EE) Failed to find a socket to listen on(EE)
x11_live_test: private Xvfb unavailable
exit=1

$ DISPLAY=:99 tools/with_display_lock.sh build/san/tests/x11_order_test
Fatal server error:
(EE) Failed to find a socket to listen on(EE)
x11_order_test: FAIL no private Xvfb
exit=1

$ DISPLAY=:65432 build/san/tests/refproto_test
refproto_test: window=0x00000000 connect DISPLAY=:65432: connection error=1
refproto_test:62: FAIL refproto_display_open(d,"refproto_test",true) == 0
exit=1
```

Bogus DISPLAY reached the failed-connect path, but LeakSanitizer reported no
heap leak on that path on this libxcb. To reproduce the allocated-connection
failure from the bead, I added the missing-XTest fixture first and rebuilt
`build/san/tests/refproto_test` (exit 0), before changing the open routine.

```text
$ DISPLAY=:99 tools/with_display_lock.sh build/san/tests/refproto_test
refproto_failed_open_test: window=0x00000000 XTEST runtime dlopen libxcb-xtest.so.0: libedit-hltk-missing-xtest.so: cannot open shared object file: No such file or directory
refproto_test:75: FAIL !missing.display.conn && !missing.display.library
ERROR: LeakSanitizer: detected memory leaks
... refproto_display_open ... tools/refwin_protocol.h:142:15
SUMMARY: AddressSanitizer: 23884 byte(s) leaked in 6 allocation(s).
exit=1
```

Leak derivation from that report (M): direct allocation 21176 bytes plus
indirect allocations 2596 + 32 + 32 + 32 + 16 = 23884 bytes; one allocation
per listed term gives six allocations. Full stacks are in
`red-refproto-missing-xtest.log`.

## Xvfb diagnosis by hand

`findmnt -T /tmp/.X11-unix -o TARGET,SOURCE,FSTYPE,OPTIONS` showed
`tmpfs ro,nosuid,nodev,relatime`; `os.access(..., os.W_OK)` returned false.
The pipe's write end was inherited as fd 4 in these manual probes (M: pipe
return value). DISPLAY was removed from the child environment.

```text
$ /usr/bin/Xvfb -displayfd 4 -screen 0 640x480x24 -nolisten tcp -noreset
_XSERVTransSocketUNIXCreateListener: ...SocketCreateListener() failed
_XSERVTransMakeAllCOTSServerListeners: failed to create listener for unix
Fatal server error:
(EE) Failed to find a socket to listen on(EE)
displayfd: b''
exit=1

$ /usr/bin/Xvfb :96 -screen 0 640x480x24 -nolisten tcp -noreset
poll: None
abstract 96 connect: 0
exit=0 (after the probe terminated its private child)

$ /usr/bin/Xvfb :96 -displayfd 4 -screen 0 640x480x24 -nolisten tcp -noreset
displayfd: b'96\n'
poll: None
abstract 96 connect: 0
exit=0 (after the probe terminated its private child)
```

Inference from these probes: automatic display selection requires the pathname
listener to succeed, whereas explicit selection tolerates a partial listener
set and runs on the abstract socket. Retaining `-displayfd` with an explicit
number still gives a readiness handshake, so no polling delay is introduced.

A manual `-displayfd ... -nolisten unix` variant also started, but advertised
display zero. No client was connected to that display. That variant was
rejected; every actual suite uses the safe inherited display or a nonzero
private server. Manual probe children were terminated and reaped.

An occupied-display check compiled the header into the scratch
`xvfb_probe.c`, held the abstract X1 socket bound, and invoked the probe under
`DISPLAY=:99 tools/with_display_lock.sh`. Output:

```text
private DISPLAY=:2
probe exit: 0
occupied probe exit=0
```

The choice is derived from the bind results: the first positive candidate was
occupied, so the next candidate was selected. The helper never connects to
the occupied listener; Xvfb's own lock/bind handles any selection race safely.

## Focused green runs

The sanitizer build command above was repeated after implementation, exit 0.
The same suite commands now produced:

```text
$ build/san/tests/journal_retire_test
(M)[AC] BAT0=unknown (status file absent) ...
journal_retire_test: passed
exit=0

$ DISPLAY=:99 tools/with_display_lock.sh build/san/tests/x11_live_test
§22 startup errors: ok
§25 window/Present/idle: ok
x11_live_test: ok
exit=0

$ DISPLAY=:99 tools/with_display_lock.sh build/san/tests/x11_order_test
x11_order_test: all ok
exit=0

$ DISPLAY=:99 tools/with_display_lock.sh build/san/tests/refproto_test
refproto_failed_open_test: window=0x00000000 XTEST runtime dlopen libxcb-xtest.so.0: libedit-hltk-missing-xtest.so: cannot open shared object file: No such file or directory
refproto_test: zero-clock bootstrap, stable 60/90 Hz, rate change, future/stale/zero clocks PASS
exit=0
```

The journal line is abbreviated after the battery stamp; raw timing fields
remain in `green-journal_retire_test.log`. The focused-run uptime above is the
load stamp for those measurements. LeakSanitizer emitted no leak report on
the passing missing-runtime regression.

The bogus-DISPLAY command was repeated: exit 1, the expected connection-error
and success-assertion diagnostics, and no LeakSanitizer report. A standalone
invalid-display invocation cannot pass the suite's existing success assertion;
the missing-runtime fixture verifies the failed-open resource contract within
the normally passing suite.

## Verification

`make -j64 all`: exit 0 (M: `make-all.exit`), gcc release build.

Full sanitizer check: pending.

## Compatibility reasoning

BAT0 hardware is not available here. When its status file opens, the same
`fgets` assertion, newline trimming, and Discharging-versus-AC decision run as
before; the absent-file suffix is empty. Journal contents, retirement checks
and fixture counts are unchanged.

A writable `/tmp/.X11-unix` is not available here. On the ThinkPad the directory
access succeeds, the fallback is not called, and Xvfb receives the original
automatic `-displayfd` command. Its pipe readiness, child ownership, exit
cleanup, dimensions and TCP settings are unchanged. The explicit caller-owned
`:99` path is also unchanged. No suite expectation was relaxed to skip startup
or tolerate a failed display connection.

## Found, not fixed

- Xvfb emits xkbcomp warnings for unresolved XF86 keysyms and warns about the
  WSLg socket-directory ownership/mode. They do not fail the passing suites;
  no socket directory permissions or keymap expectations were changed.
- The baseline bench/editor power-stamp code actually uses `[unknown]` for an
  unknown battery state. This bead follows the brief's explicit desktop AC
  requirement for the journal measurement only; those other readers were not
  changed.

## Coordinator decisions

Pending final verification. The BAT0/writable-directory hardware paths are
reasoned above, as the brief permits; they have not been executed on this box.

## Coordinator verification (session 22)

The worker's Codex session ended with `ERROR: Selected model is at capacity` 48 suites into its full check (08:29). The coordinator re-ran the full sanitizer check on this tree with LeakSanitizer on, on display :95 while W0.4's own check held :99:

```
DISPLAY=:95 make -j64 check   → exit 0, 104 suites (102 test binaries + 2 shell contracts)
start 08:30:34 load1 1.63, end 08:43:01 load1 0.83  (M)[AC, desktop Spiral]
```
