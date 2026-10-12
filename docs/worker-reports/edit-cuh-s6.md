# edit-cuh worker report

Implemented only edit-cuh in the assigned worktree. No `bd` or `git` commands
were run. Decision record: `docs/decisions/edit-cuh.md`.

## Files touched

- `src/x11/x11.c`: restrict rebuild exemptions to direct library calls.
- `src/x11/input.c`: exempt direct XKB state construction/warm-up calls, keeping
  our preparation body and loop control counted.
- `tests/keyboard_guard_test.c`: server-free own-code/library allocation probes,
  balanced success/cancellation/failure scopes, and real XKB state preparation.
- `tools/test_power_stamp.py`: compile the actual STAMP preambles with injected
  BAT0/load files and check printed battery, AC and unknown tags.
- `docs/decisions/edit-cuh.md` and this report.

The STAMP implementations in `tests/editor_test.c` and `bench/editor_bench.c`
already derived their tags from BAT0 in the supplied worktree. They were
verified and left intact. None of the excluded editor implementation files
was edited.

## Guard red-green

BAT0 was `Not charging`, so allocation observations below are (M)[AC].
The test uses (P) one `sizeof(char)` allocation per injected probe. Case
indices derive from the fixture array order: success, initially cancelled,
context failure, keymap failure, state failure. Successful own probes comprise
three cancellation checks + preparation entry + publication + final balance
probe = six. Cancellation/context failure comprise one check + publication +
balance = three; keymap failure comprises two checks + publication + balance
= four; state failure reaches the same probes as success = six. The red
count of two is publication + balance: the broad bracket hides the others.
These are event counts, not timing/performance allowances. Exit codes use
the conventional C success/failure values (P).

After writing the test, before implementing the fix:

```text
$ make build/tests/keyboard_guard_test
exit=0
$ build/tests/keyboard_guard_test
keyboard_guard_test: case=0 own_probes=6 counted=2 guard=active FAIL
keyboard_guard_test: case=1 own_probes=3 counted=2 guard=active FAIL
keyboard_guard_test: case=2 own_probes=3 counted=2 guard=active FAIL
keyboard_guard_test: case=3 own_probes=4 counted=2 guard=active FAIL
keyboard_guard_test: case=4 own_probes=6 counted=2 guard=active FAIL
exit=1
```

After narrowing the brackets and preserving direct library exemptions:

```text
$ make build/tests/keyboard_guard_test
exit=0
$ build/tests/keyboard_guard_test
keyboard_guard_test: case=0 own_probes=6 counted=6 guard=active PASS
keyboard_guard_test: case=1 own_probes=3 counted=3 guard=active PASS
keyboard_guard_test: case=2 own_probes=3 counted=3 guard=active PASS
keyboard_guard_test: case=3 own_probes=4 counted=4 guard=active PASS
keyboard_guard_test: case=4 own_probes=6 counted=6 guard=active PASS
exit=0
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/keyboard_guard_test
keyboard_guard_test: case=0 own_probes=6 counted=0 guard=ASan-inert PASS
keyboard_guard_test: case=1 own_probes=3 counted=0 guard=ASan-inert PASS
keyboard_guard_test: case=2 own_probes=3 counted=0 guard=ASan-inert PASS
keyboard_guard_test: case=3 own_probes=4 counted=0 guard=ASan-inert PASS
keyboard_guard_test: case=4 own_probes=6 counted=0 guard=ASan-inert PASS
exit=0
```

Sanitizer zero counts derive from the guard's existing no-op implementation
(P); logical exemption-scope assertions still run. Leak detection is disabled
only in verification commands: the sandbox's ptrace environment makes
LeakSanitizer terminate with `LeakSanitizer does not work under ptrace`.
AddressSanitizer and UndefinedBehaviorSanitizer remain enabled.

## STAMP regression

There is no production red run for this item: both paths already pass in the
supplied tree. The new regression was also checked against temporary,
in-memory fixed-AC mutations of each extracted snippet, compiled only in
temporary files. No production source was reverted to manufacture a red run.
The (P) Discharging fixture must print `[bat]`; both negative controls failed:

```text
AssertionError: editor_bench: 'Discharging\n': 'STAMP (M)[AC] BAT0=Discharging load1=unknown shared box\n' != 'STAMP (M)[bat] BAT0=Discharging load1=unknown shared box\n'
exit=1
AssertionError: editor_test: 'Discharging\n': 'edit-d5o STAMP (M)[AC] BAT0=Discharging load1=unknown shared box\n' != 'edit-d5o STAMP (M)[bat] BAT0=Discharging load1=unknown shared box\n'
exit=1
```

Green, using the actual worktree sources:

```text
$ python3 tools/test_power_stamp.py
editor_bench: BAT0 status -> STAMP battery/AC/unknown fixtures PASS
editor_test: BAT0 status -> STAMP battery/AC/unknown fixtures PASS
exit=0
```

Fixtures derive from kernel status spellings (Discharging, Charging, Full,
Not charging, Unknown), plus unknown spelling, empty and missing files (P).
They verify both the raw status and the printed tag. No timing workload runs.

## Verification

Release `make all` passed with the required compiler/warning configuration.
`build/tests/x11_input_test` printed `x11_input_test: ok` and succeeded.
The editor bench's typing-accounting and settle self-checks passed.
`build/bench/x11_bench` completed translation accounting but failed its X11
startup path with `plat_init failed (-1) with supplied DISPLAY`; the error is
the existing no-display return code (P). It checked BAT0 before measurement.

Final incremental release build, including the final regression source:

```text
$ make all
exit=0
```

The unmodified `make check` built the sanitizer binaries but LeakSanitizer
terminated in this sandbox. With only leak detection disabled, it reached
the display lane and failed:

```text
$ ASAN_OPTIONS=detect_leaks=0 make check
== build/san/tests/cli_test
waiting for display :99 (held by unknown)
cli_test:391: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
cli_test: §36 forced startup timeout correctly failed
cli_test:391: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
make: *** [Makefile:133: check] Error 1
exit=2
$ ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 build/san/tests/x11_identity_test
x11_identity_test:25: FAIL plat_init(&p, &cfg) == PLAT_OK
exit=1
```

Here source line numbers/section labels identify existing code; exit values
are conventional test/make failure codes (P). Display `:99` is the designated
coordinator Xvfb environment, not an invented fixture or measurement budget.

Continued sanitizer binaries serially after the stopped display lane, using
the same ASan/UBSan build and `ASAN_OPTIONS=detect_leaks=0`. Full per-suite
outputs and exits are in `build/edit-cuh-logs/`; the enumerated continuation
is `non-display-summary.log` and the additional editor/headless continuation
is `headless-summary.log`. All executed suites succeeded except the failures
identified below. In particular:

```text
keyboard_guard_test: exit=0
x11_input_test: exit=0
x11_alignment_test: exit=0
x11_display_guard_test: exit=0
editor_hot_exit_test: exit=0
editor_scroll_test: exit=0
editor_session_test: exit=0
editor_test --headless: exit=0
FILE_KILL_CANCEL_ONLY=1 file_kill_test: exit=0
wayland_bench_test: exit=0
wayland_gl_test: exit=0
wayland_test: exit=0
```

The cancellation-only selector is the existing boolean fixture selector (P).
`tools/test_replay_cli.sh build` also passed. The display-lock contract step
was not reached by the failed full check and was not run separately against
the shared display lock.

Wayland output:

```text
wayland_test: selection and headless lifecycle passed
wayland_test: decoder exercised; allocation count inert under sanitizers
wayland_test: native key precedes simultaneous worker wake
wayland_test: mock configure/ack, xkb keyboard/repeat/focus, blink/work timers, pointer, clipboard receive/send/cancellation, frame/presentation, discard, cleanup and missing-global checks passed
wayland_gl_test: frame callback is only a hint; mocked fence T5 and matching presentation T6, stale/duplicate/discard checks passed
```

The named trace stages are existing protocol identities (P). There is no real
Wayland compositor in this sandbox. These tests use the existing headless
policy paths and local protocol mock; real compositor input/clipboard
integration, EGL/GL drawing, device fences and presentation timing still
require a real compositor/device. No Wayland source was changed.

## Outside scope and what remains

- **Display unavailable in this sandbox.** Direct X11 initialization fails.
  `refproto_test` also requires X11 despite its injected protocol operations:
  it printed `connect DISPLAY=:99: connection error=1`. `file_kill_test` is
  absent from Makefile's display filter but calls the X11 `view_init` from
  `bench/file_bench.c`; its viewport and queued open/save children fail here.
  Its cancellation-only path passes. `editor_close_test` passes its headless
  close case and fails its native close case. The coordinator must rerun
  display suites on Xvfb `:99`, including release
  `build/tests/x11_order_test rescans` to check real keymap rebuilding. The
  connection error is the existing XCB failure code (P).
- **IPC socket restrictions.** `ipc_test` fails at socket initialization with
  `init rc=4 errno=1 (Operation not permitted)`. These are the existing IPC
  error enum and EPERM code (P), not performance measurements.
  `editor_large_test` and `editor_p4_test --headless` reach their IPC setup
  checks and fail there too. Their preceding null-backend checks pass.
  These need a host that permits the IPC socket; they were not changed.
- **Unrelated journal kill-test fixture failure.** The default sanitizer run
  and its serial rerun both print:

  ```text
  journal_kill_test: FAIL trial=0 target=1 issued=1 page_cache=1 ack=0 replay=0 rc=6 error=0
  exit=1
  ```

  Counter observations are (M)[AC], BAT0 `Not charging`. The initial trial is
  the existing before-pump case, so its target is the first issued edit and
  durable acknowledgement is absent. `rc=6` is `JOURNAL_CALLBACK` (P), the
  existing enum's callback-rejection value. The replay callback in
  `tests/journal_kill_test.c` rejects WINDOW records unless their size is the
  legacy shape. `journal_set_window` now writes `JOURNAL_WINDOW_BYTES`:
  width + height + flags = three 32-bit fields = 12 bytes (P, wire ABI);
  the fixture expects width + height = two 32-bit fields = 8 bytes (P, legacy
  wire ABI). The stale fixture is outside edit-cuh and remains unchanged.
- **Other fixed power labels.** The STAMP paths are dynamic, but review
  measurement messages elsewhere in `tests/editor_test.c` still contain
  literal `(M)[AC]`; `bench/editor_bench.c`'s G11 accounting self-check also
  contains a fixed label. These are outside the requested STAMP follow-up and
  were not edited.
- **LeakSanitizer verification remains host-only.** The sandbox's ptrace
  limitation prevents leak checks. ASan/UBSan checks ran with leak detection
  disabled; normal leak-enabled full verification remains for the coordinator.

No implementation work remains within edit-cuh. The broader non-display
requirement is not fully green because of the IPC restriction and unrelated
journal fixture failure. No outside-scope fix, new bead, or tracking command
was attempted. Stop after this report.
