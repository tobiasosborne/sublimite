# edit-mdv slice 3 — session 9 worker report

Scope: P4.7b savectl review fixes in `docs/reviews/P4-modules-2.md`, in the
requested order §19, §20, then the standalone §18 contract. Implementation and
regressions are complete within that scope. Acceptance remains incomplete:
the requested full-duration clean fuzz run is blocked by pre-existing fuzzer
oracle assertions reproduced against the original controller and fuzzer.
No editor, file or journal implementation files were changed. No new globals
were introduced. Git was used only for read-only diff/show; no `bd` invocation,
HANDOFF.md edit or docs/worklog edit was made.

## §19 — prepare handoff and finish reacquisition: done

A durable prepare mailbox message now returns whole-session journal ownership
before file-only work starts. Its retained token is copied to UI storage before
an atomic acknowledgement permits the worker continuation. Saturation and
waiting for that receipt yield the bulk lane. The borrowed token is available
while file work is active but journal_leased is false. Finish acquires a new
exclusive lease, with a quiescent private journal and a complete CURRENT session
checkpoint; accepted edits after the cutoff remain represented in order.

The new crash regression holds the file worker at FILE_STEP_FSYNCED, appends an
edit/view for a second buffer after ownership handoff, kills the process before
rename without flushing, and verifies that edit replays while the target remains
unchanged. Existing save transaction tests now pump the prepare handoff and
append the post-cutoff edit during file work. No journal API change was needed.

Red: failing crash assertion was written and run before the handoff fix.

```text
$ DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/san/tests/savectl_test --journal-handoff
tests/savectl_test.c:383: !savectl_get_model(controller).journal_leased
tests/savectl_test.c:391: WIFSIGNALED(status) && WTERMSIG(status)==SIGKILL
```

Green after implementation:

```text
$ DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/san/tests/savectl_test --journal-handoff
savectl prepare handoff/other-buffer crash replay: ok
```

## §20 — fresh-inode finish recovery: done

The public contract now distinguishes retryable append/directory failures from
fdatasync writeback loss. `savectl_recover_finish` enqueues complete CURRENT
session rotation into a fresh inode followed by retained-token finish
reconciliation. Ordinary retry/finish cannot repair lost writeback. Rotation
and reconciliation failures retain needs_finish and the token; only successful
finish acknowledges saved and retires the retained file. Other buffers and
in-flight tokens must remain in the supplied complete checkpoint.

The regression injects writeback loss during old-generation finish flush,
confirms repeated journal_retry and ordinary finish remain IO, fails a fresh
rotation, then succeeds and checks exact replay of both buffers. It holds the
old journal descriptor during the inode assertion to prevent inode-number reuse
across successive rotations from producing a false failure.

Red: the new public interface initially had a rejecting body, and the complete
regression failed at recovery admission before implementation.

```text
$ DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/san/tests/savectl_test --finish-recovery
tests/savectl_test.c:460: savectl_recover_finish(f.s,next,4)==0
```

Green after implementation:

```text
$ DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/san/tests/savectl_test --finish-recovery
savectl finish writeback/fresh-inode retry/retained-token reconciliation: ok
```

## §18 — bounded request/preparation and status-frame hook: scoped work done

`savectl_save_request` copies a fixed-size request containing an already pinned
immutable session root/context and selected-buffer snapshot. The host must
publish those roots using bounded mutation bookkeeping; capturing them cannot
enumerate buffers, allocate/copy a checkpoint or construct a new snapshot.
A worker-only callback builds the complete checkpoint. Admission refusal leaves
ownership with the caller; success transfers snapshot/context ownership, with
worker-only release after snapshot retirement even on validation/preparation/file
failure. All arena and backing leases survive their worker uses.

The model exposes request identity, input timestamp, pending saving-frame state
and submitted timestamp. It preserves status "saving" until
`savectl_status_frame_submitted` records a successful submission of that exact
request's status frame, even if worker completion arrives first. Stale IDs,
duplicates and timestamps before input are rejected. Tick/mailbox receipt and
render preparation do not acknowledge frame submission.

The controller-only regression uses a synthetic session of 2048 buffers (M)[AC]
and holds the bulk worker before admission. The release counting allocator
reports zero request-path allocations (M)[AC], while normal-build syscall guards
prohibit UI I/O. Preparation and release occur only on the worker. It covers
successful and failed preparation, completion before the status hook, and
pending/stale/duplicate submission behavior. Its clock values are synthetic
protocol inputs, not latency measurements. Actual busy-backend rendering and
large checkpoint-payload acknowledgement measurement remain the editor bead's
responsibility; no end-to-end G8s gate pass is claimed.

Red: the request interface first had a rejecting body; the regression failed
at admission before the implementation.

```text
$ DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/san/tests/savectl_test --bounded-request
tests/savectl_test.c:539: savectl_save_request(f.s,&request)==0
```

Green after implementation:

```text
$ DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/san/tests/savectl_test --bounded-request
savectl bounded session request/worker preparation/submitted-status hook: ok
savectl bounded session request/worker preparation/submitted-status hook: ok
$ DISPLAY=:99 build/tests/savectl_test --bounded-request
savectl bounded session request/worker preparation/submitted-status hook: ok
savectl bounded session request/worker preparation/submitted-status hook: ok
```

## Decisions and exact editor integration obligation

`docs/decisions/edit-mdv.md` records the prepare ownership protocol, complete
fresh-inode recovery choice, context/allocator lifetimes, and the exact future
editor call sequence: timestamp input before bounded capture; pin published
root/snapshot; call savectl_save_request; render/tag pending saving status; call
savectl_status_frame_submitted only after actual successful backend submission;
route/tick prepare; append staged edits after journal ownership returns; finish
with a complete CURRENT checkpoint, selecting recover_finish on writeback loss.
The editor test must measure input through actual submitted status, including
substantial dirty checkpoint data and a busy backend. `src/editor` is untouched.
The legacy tree/checkpoint entry point remains compatible but cannot establish
that acknowledgement bound.

Minimal fuzz changes cover receipt-driven prepare handoff, journal appends during
file-only work, active retained-token availability, and the explicit recovery
entry point in the existing finish-failure scenario.

## Verification and missing acceptance

Power is AC [AC], confirmed by the provided environment and BAT0 status
"Not charging". The box remained loaded; no single timing sample was treated
as a gate and no new performance verdict or benchmark measurement was made.
Compiler/warning configuration is gcc 13 release and clang 18 ASan/UBSan with
C11, -Wall -Wextra -Werror -Wshadow -Wconversion. LeakSanitizer is disabled with
ASAN_OPTIONS=detect_leaks=0 because it cannot run in this sandbox; the coordinator
must rerun leaks enabled. Every display-dependent invocation uses DISPLAY=:99
and EDIT_DISPLAY=:99. No invocation used the real display.

```text
$ DISPLAY=:99 EDIT_DISPLAY=:99 make -j4 all
exit 0
$ DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make -j4 check
check: 60 test binaries passed
== tools/test_replay_cli.sh
test_replay_cli: all passed
exit 0
```

The final full sanitizer check passed 60 test binaries (M)[AC] and the replay
CLI shell suite, including the updated savectl binaries. Both savectl binaries
also passed independently. The final release build exited zero with all warning
flags enabled. `git diff --check` is clean.

The first concurrent all/check verification raced their shared release archive;
serial all passed. The initial sandbox display check could not create Xvfb's
local Unix socket. The final suite was rerun with authorized local display access
on the existing :99 Xvfb server, without changing source or Makefile. Its lengthy
raster_test completed normally; no unrelated test failure remains.

Requested fuzz command (60 seconds (G)) was run after implementation; it aborted
at an existing reload oracle before completing, so a clean full-duration run
is **missing**, not reported as passed:

```text
$ DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/fuzz/savectl_fuzz -max_total_time=60 -max_len=64 -artifact_prefix=build/worker-logs/
fuzz/savectl_fuzz.c:254: assertion failed: savectl_take_reload(s, &replacement, &view) == 0
SUMMARY: libFuzzer: deadly signal
reproducer bytes: 0a
exit 77
```

Out of scope and unchanged: the acquisition allocation-failure oracle clears
its fault then immediately calls take_reload, but the existing contract requires
retirement and explicit reacquisition before installation. Compiling both the
original savectl implementation and original fuzzer from read-only git show
reproduces the same input/assertion:

```text
build/worker-logs/baseline-fuzz.c:248: assertion failed: savectl_take_reload(s, &replacement, &view) == 0
exit 77
```

Another pre-existing oracle mismatch, also confirmed against original sources:
input 02 expects SAVECTL_FAILED for JOURNAL_BASE_CHANGED, while the already landed
§10 contract classifies it as SAVECTL_EXTERNAL_MODIFIED with recovery actions:

```text
build/worker-logs/baseline-fuzz.c:144: assertion failed: model.state == SAVECTL_FAILED
exit 77
```

Those oracle changes were not made because the worker instruction explicitly
requires reporting out-of-scope findings without fixing them. The coordinator
must resolve/reassign them and rerun the complete fuzzer, then rerun leaks on.
Other review findings and actual editor integration remain separate scope.
