# edit-6pc s9 worker report

Scope: P2-1 clipboard review §1 (borrow lifetime), §26 (completion ring),
§27 (MULTIPLE saturation ordering), §28 (nonblocking admission and eager pool).
Implementation is complete for every named finding. GCC make all, focused release
and sanitizer regressions, clipboard integration and live fuzz are green. The
final-tree full make check requirement is NOT met: unrelated refwin_test preflight
failed on shared-display input state. Complete evidence and limitations follow.

Changed code: src/x11/clip.c and src/x11/clip.h (admission/lifetime documentation). Added tests/x11_clip_review_test.c, automatically
included by the existing Makefile. Design: docs/decisions/edit-6pc.md. No git writes,
bd, HANDOFF.md, docs/worklog edits, new globals or typing-path allocations.

## Per-finding red/green evidence

Each regression was written and run before its corresponding implementation change.
Focused tests use clang 18 ASan/UBSan with DISPLAY=:99 EDIT_DISPLAY=:99
ASAN_OPTIONS=detect_leaks=0. The focused binary includes the clipboard implementation
for deterministic internal state construction and uses real X transport. Existing
raw-peer integration tests exercise the normal compiled module and event loop.
All below exit statuses are measured outcomes (M)[AC].

§1: make_room no longer drops the paste buffer when the event queue is empty.
The regression borrows a local paste, rejects a replacement at the memory limit,
then admits one at a larger limit while proving the previous bytes remain live.

Red: build/san/tests/x11_clip_review_test (exit 1)
```text
FAIL: budget replacement must refuse while previous paste is borrowed
```
Green: same binary (exit 0)
```text
clipboard review §1: PASS
```

§26: admission bounds aggregate requests across ready, confirming, remote and
pending-failure states. Emission checks ring capacity; partial local/remote batches
retain their bytes and remaining counts. Deferred failures/ownership notices are
emitted later. The test replaces ownership between a full batch and confirmation,
requires refusal of excess admission, verifies partial emission and replacement
between emissions, and checks exactly one completion per accepted request.
Additional coverage verifies a full ring of deferred failures and remote partial
completion/data association.

Red: build/san/tests/x11_clip_review_test 26 (exit 1)
```text
FAIL: aggregate outstanding cap includes ready and confirming requests
```
Green: same binary, with no drops and exact completion/data checks (exit 0)
```text
clipboard review §26: PASS
```
Additional failure-accounting coverage caught and corrected an edge case in this
refactor: charged receive capacity must be released even when no sink was allocated.
Red before correction: build/san/tests/x11_clip_review_test 26 (exit 1)
```text
FAIL: failed receive releases charged capacity even without a sink blob
```

§27: both invalid requests and otherwise-valid requests at full capacity use ordered
refusal. When no storage can retain the refusal, older same-tuple jobs are retired in
arrival order before the newest refusal. Tests fill the entire job table without
polling, check no newer failure overtakes retained jobs, exercise both refusal paths,
and verify slot reuse. This deliberately permits older unfinished conversions to
fail under saturation; other request tuples are retained.

Red: build/san/tests/x11_clip_review_test 27 (exit 1)
```text
FAIL: saturated refusal must not overtake retained older MULTIPLE jobs
```
Green: same binary (exit 0)
```text
clipboard review §27: PASS
```

§28: admission no longer sleeps awaiting deferred frees. It checks once, drains at
most one bounded mailbox slice and refuses immediately if the bytes still do not
fit; retry after completion succeeds. The aligned clipboard pool is created during
clipboard initialization, never lazily in runtime dispatch. The regression blocks
the worker, queues a large free behind it, tries admission repeatedly, releases the
worker and verifies budget reclamation and successful retry. A separate assertion
checks eager initialization and required alignment.

Red: build/san/tests/x11_clip_review_test 28 (exit 1)
```text
FAIL: admission must return promptly rather than sleep for deferred frees
FAIL: clipboard worker must be initialized before runtime dispatch
blocked-worker admission average (M)[AC]: 77806726 ns over four back-to-back calls
```
Green: same binary (exit 0)
```text
clipboard review §1: PASS
clipboard review §26: PASS
clipboard review §27: PASS
blocked-worker admission average (M)[AC]: 81707 ns over four back-to-back calls
clipboard review §28: PASS
```
Admission timing is a repeated-call regression guard, not a platform performance
gate verdict. Measurements are (M)[AC] on the loaded box; BAT0 reports Not charging.
The test's coarse average guard is 2 ms (G), rather than treating one timing sample
as a gate pass/fail. No module benchmark or real-display latency claim was made.

## Verification and remaining work

Final GCC 13 release build:
```text
DISPLAY=:99 EDIT_DISPLAY=:99 make -j4 all build/fuzz/x11_input_fuzz
exit 0
```
All C compilations use -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion.
The initial complete sanitizer run also exited successfully, including replay CLI.
Clipboard accounting was corrected during that run, so final-tree runs were made.
Those later full runs failed in unrelated refwin_test; their evidence is below.
Remaining final-tree sanitizer binaries and replay CLI were subsequently verified
separately. This is not a claim that final make check passed.

Focused corrected-code green output (exit 0):
```text
clipboard review §1: PASS
clipboard review §26: PASS
clipboard review §27: PASS
blocked-worker admission average (M)[AC]: 78796 ns over four back-to-back calls
clipboard review §28: PASS
```
This §26 pass includes the additional failure-accounting regression above.
Release build and test outcomes are (M)[AC], Not charging, load1 3.76 (M)[AC]
at the final-suite launch. LeakSanitizer cannot run in this sandbox, so all sanitizer runs
use ASAN_OPTIONS=detect_leaks=0; coordinator must rerun with leaks enabled.

No named finding is left unimplemented. Clipboard-only pool alignment is included
because §28 explicitly requires it; the unrelated x11_runtime allocation from review
§6 is outside this bead and remains untouched. Existing synchronous plat_clip_set
copying, inline-free fallback when deferred-free storage is full, serving Latin-1
conversion, and clipboard deadline polling remain as documented in P2.2h, outside
this bead. No out-of-scope implementation changes were made.

Additional out-of-scope admission observation (code inspection): set_fits subtracts
an unshared old owner's capacity as reclaimable, but install may defer that large
blob's free. Until the free completion is drained, accounting can temporarily
include both blobs and exceed the configured budget. This predates this bead when
the clipboard pool was already active; eager pool initialization makes that path
available immediately. It does not invalidate the retained paste borrow (which
prevents reclamation via its additional reference), and was not changed here.
Coordinator should evaluate it separately from the named findings.

## Final-suite transient outside the bead

The first complete make check passed all 61 binaries plus replay CLI (M)[AC].
The next, final-tree run exited 2 (M)[AC] at the unrelated refwin_test after:
```text
== build/san/tests/refwin_test
keyinject: --pairs must be nonzero
refwin_test: measured period arithmetic and 90/60 Hz pair rejection PASS
refwin_test: exact NotifyMSC X error, event timeout and zero-clock diagnostics PASS
make: *** [Makefile:101: check] Error 1
```
Its existing preflight returns failure without identifying the failing check.
The immediate isolated rerun exited 0 (M)[AC] and printed:
```text
refwin_test: dry-run checks mapped/focusable targets, preserves focus/CSV, rejects missing targets/wrong rates, injects no keys PASS
refwin_test: PASS 12 editor/reference pairs, complete monotonic CSV, incomplete/wrong-frame runs rejected (Xvfb phase approximate)
```
The root cause is unconfirmed; shared-display interference is a possibility, not a
diagnosis. No refwin/editor/display code was changed. A further complete final-tree
run also failed as recorded below. The isolated success does not erase this failure.

The final-tree full-suite retry also exited 2 (M)[AC], again in refwin_test. This
run supplied a concrete shared-input diagnostic:
```text
refwin_test: dry-run RED: exit=1 expected=1 diagnostic=keyinject: window=0xffffffff keyboard is not idle: keycode=38 down (release keys before preflight)
 expected=GetWindowAttributes failed
make: *** [Makefile:101: check] Error 1
```
A held key on the shared Xvfb display displaced the expected window-attribute
error. Its owner/source is unknown. The worker did not release unknown held keys,
restart the shared server, edit refwin, or remove the test from make check.
The final-tree full make check acceptance is therefore not met, despite the earlier
complete green run and the passing isolated refwin rerun. Remaining binaries were
subsequently run separately so the clipboard/module verification is complete.

## Remaining final-tree sanitizer coverage

Every sanitizer binary after refwin_test in the normal lexical order completed
successfully in a separate driver, including the new regressions and existing
clipboard integration tests. All earlier binaries had passed in the final full
retry before refwin_test. This establishes binary coverage except for the unrelated
refwin preflight; it does not substitute for a green full make check.

The remaining-binary driver itself exited 127 (M)[AC] after those binaries because
it used an incorrect replay-script path. The correct command was then run:
```text
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 sh tools/test_replay_cli.sh build
test_replay_cli: all passed
exit 0
```

Final clipboard regression output from that remaining-binary run (M)[AC]:
```text
== build/san/tests/x11_clip_review_test
clipboard review §1: PASS
clipboard review §26: PASS
clipboard review §27: PASS
blocked-worker admission average (M)[AC]: 95418 ns over four back-to-back calls
clipboard review §28: PASS
```
Existing clipboard raw-peer integration output: x11_clip_test: ok (M)[AC].
The existing x11_input_fuzz target is the clipboard-capable module fuzzer; live
raw-peer transactions were explicitly enabled. Final command and output:
```text
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 EDIT_X11_FUZZ_LIVE=1 build/fuzz/x11_input_fuzz -max_total_time=60 -max_len=512 -artifact_prefix=build/edit-6pc/
Done 4081 runs in 61 second(s)
exit 0
```
4,081 runs in 61 seconds, no crashes or ASan/UBSan errors (M)[AC].
Launch stamp: Not charging [AC], load1=7.49 (M)[AC]. The requested duration was
60 seconds (G); libFuzzer completed its final input before stopping.

## Final done versus remaining

Done: every named finding has a recorded failing regression before its fix and a
passing regression afterward; GCC 13 make all exits 0; required warning flags are
clean; clipboard sanitizer/integration tests pass; the live clipboard-capable fuzz
campaign completed cleanly. No additional implementation scope was added after the
requested cutoff. Design choices are in docs/decisions/edit-6pc.md.

Remaining acceptance: a complete final-tree make check exit 0 is still required.
The unrelated refwin preflight failure was reported rather than fixed or bypassed.
LeakSanitizer was disabled throughout because of the sandbox, and the coordinator
must rerun with leaks enabled. The separate existing deferred-owner-free accounting
observation above also needs coordinator review; it is outside the named findings.
No clipboard finding is left partially implemented.
