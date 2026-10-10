# edit-k60 — P4.I IPC wiring gaps

Implemented the disconnected-wait sweep and primary/isolated launcher split.
All named regressions were written and run red before production changes.
The focused release suites, final release build and full sanitizer suite are
green. Both editor and IPC fuzz campaigns are clean. Leak-enabled validation
remains the coordinator's check.

## Scope and findings

Read `CLAUDE.md`, the supplied bead, the named integration report/decisions,
`src/ipc/ipc.h`, `docs/decisions/P4.4.md`, and editor/IPC module statuses.
Also read the launcher/ownership contract in `P4.9.md` and `P4.9c.md`.
Read `git show wt/edit-og2 --stat` and its main/socket diff. Its display-routing
regions are unchanged; the launcher insertion is below them at session-path
construction. IPC already implements every primitive needed for this bead.

| Named finding | Done | Regression coverage |
| --- | --- | --- |
| Disconnected wait associations stay occupied until their buffers close | After a drain changes `wait_drops`, clear dead fixed associations and every matching buffer token. Preserve open buffers and live waits. | Null editor opens more disconnected requests than `IPC_MAX_CLIENTS` while keeping their buffers open and another wait live; checks member cleanup, slot reuse, close/reopen and an allocation guard. |
| Primary `--wait` does not split/adopt and opens with token zero | Fork before UI/worker/session-file creation, adopt in the child, pass its token to the initial request, block the parent on closed/error. | Compile production main into IPC tests with null backend and a pipe-driven loop. Check distinct launcher/UI processes, nonzero live token, file plus binary stdin membership, partial closure, full closure, UI death, inherited endpoint/lock release, and nonwait execution. |
| Isolated `--new-instance --wait` does not split/adopt | Use `ipc_server_init_isolated` and the same launcher/adoption path; preserve the existing nonwait isolated branch. | The same real main tests run with an incumbent endpoint present. Check isolated server ownership, all-member wait, UI death, and unchanged nonwait execution. |

`src/editor/buffers.c` owns both associations and the actual drain call; the
change is local to that region. `src/main.c` owns launcher startup. No IPC
implementation/header change, editor typing/capacity/index/indent change, or new
global was needed. Git remained read-only; no `bd`, `HANDOFF.md`, or worklog edits.

## Test-first red runs

The tests compiled before any production edits. The ordinary sandbox denied
Unix socket creation before reaching the regressions; the following actual red
runs used approved local-socket execution and a null backend. All displays were
`:99`. Exit status for each red run was 1 (M)[AC].

```text
$ env DISPLAY=:99 EDIT_DISPLAY=:99 ./build/tests/editor_p4_test
editor_p4_test:429: FAIL wait_count(e) == 1 && member->wait_token == 0
editor_p4_test:448: FAIL ipc_wait_sweep(&server) == 0

$ env DISPLAY=:99 EDIT_DISPLAY=:99 ./build/tests/ipc_test main_launcher_primary
main launcher primary: split=0 token=0 live=0 tabs=2 remaining=0 server=1
FAIL tests/ipc_test.c:713: accepted

$ env DISPLAY=:99 EDIT_DISPLAY=:99 ./build/tests/ipc_test main_launcher_isolated
main launcher isolated: split=0 token=0 live=0 tabs=2 remaining=0 server=0
FAIL tests/ipc_test.c:713: accepted
```

The standalone IPC server already makes a disconnected token nonlive. The
first red proves the missing editor-side ownership cleanup after that server
retirement; it does not claim a new server liveness bug.

## Green runs

The release editor suite passed with the allocation guard active. The following
is the pasted relevant output; unchanged test rows are omitted. Exit 0 (M)[AC].

```text
$ env DISPLAY=:99 EDIT_DISPLAY=:99 ./build/tests/editor_p4_test
editor_p4_test: null disconnected wait sweep, retained buffers, association reuse and allocation guard passed
editor_p4_test: null 100 tabs 10000 mixed keys all modules mallocs=0 guard=active
editor_p4_test: raster 100 tabs 10000 mixed keys all modules mallocs=0 guard=active
editor_p4_test: all passed
```

The final full release IPC suite also passed. These are its terminal assertion
lines; journal diagnostics containing temporary paths are deliberately omitted.
Exit 0 (M)[AC].

```text
$ env DISPLAY=:99 EDIT_DISPLAY=:99 ./build/tests/ipc_test > build/k60-ipc-green.log 2>&1
ipc_test: main primary launcher split/adopt/all-member wait/UI death/nonwait passed
ipc_test: main isolated launcher split/adopt/all-member wait/UI death/nonwait passed
ipc_test: parser, wire, roundtrip, --wait, stdin, stale, race, fragments, tokens, endpoints, review-fix suites ok
```

## Decisions and bounds

Design choices are recorded in [edit-k60.md](../decisions/edit-k60.md).
The sweep runs once after an affected drain, using an automatic bounded token
array and the existing fixed buffer reservation. It clears both ownership
sides without closing buffers or modifying live wait counts. The existing
input-to-submit allocation barrier remains in force.

The launcher forks while the server has accepted no peers and before creating
workers/UI state. The parent clears its path ownership and closes/invalidates
its inherited lifecycle-lock descriptor before finalizing its server copy.
Only the UI retains endpoint cleanup ownership. This prevents parent cleanup
from unlinking the UI endpoint, holding its lock, or retaining its wait channel.
No new API or global is introduced.

The final endpoint probe uses the UI's reported socket leaf rather than
assuming the legacy socket name. It checks the socket inode, independently
acquires/releases the lifecycle lock, and connects to the actual endpoint.
This allows `edit-og2`'s display suffix to land without a test routing change.
The final release IPC suite, both compiler builds and the repeated full
sanitizer suite pass after this refinement. Production code and fuzz targets
are unchanged from the clean campaigns below.

No separate performance campaign was run: this change adds startup and
off-input drain work. The existing full-check harness invokes benchmark rows;
their loaded-box timings are not treated as acceptance verdicts here. Release
typing workloads measured zero
allocations (M)[AC], and the new sweep guard passed. ASan's allocation guard is
inert, so allocation evidence comes from the release run.

## Final verification

Power was `Not charging`, with AC supplied by the task. Pre-fuzz load averages
were 10.27 / 5.56 / 4.25 (M)[AC]. No samples were used as latency gate evidence.
Compiler versions were gcc 13.3.0 and clang 18.1.3. Build flags include C11,
`-Wall -Wextra -Werror -Wshadow -Wconversion`.

```text
$ env DISPLAY=:99 EDIT_DISPLAY=:99 make -j4 all > build/k60-all-final.log 2>&1
[exit 0]

$ env DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make -j4 check > build/k60-check-final.log 2>&1
check: 60 test binaries passed
== tools/test_replay_cli.sh
ok:   --speed=inf rc=2 replay: --speed must be a finite number > 0
ok:   --speed=nan rc=2 replay: --speed must be a finite number > 0
ok:   --speed=0 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=-1 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=2 rc=0
test_replay_cli: all passed
[exit 0]

$ env DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 ./build/fuzz/ipc_fuzz -max_total_time=60 -max_len=4096 > build/k60-ipc-fuzz.log 2>&1
Done 920240 runs in 61 second(s)
[exit 0]

$ env DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 ./build/fuzz/editor_fuzz -max_total_time=60 -max_len=512 > build/k60-editor-fuzz.log 2>&1
Done 6607 runs in 61 second(s)
[exit 0]
```

Build/check/fuzz exit results, test counts, executions and duration above are
(M)[AC]. Each fuzz
campaign exceeded the requested 60-second budget (G) cleanly, with no sanitizer
finding. Full `make check` compiled and passed the final main/editor sources with
clang ASan/UBSan and the same display/leak setting. Release `make all` used gcc;
both configurations were clean under every required warning flag.

The main patch also dry-applied to `wt/edit-og2`'s main source with
`patch --dry-run --fuzz=0`, exit 0 (M)[AC]:

```text
checking file main.c
Hunk #1 succeeded at 39 (offset 1 line).
Hunk #2 succeeded at 79 (offset 1 line).
```

This is a text compatibility check in ignored build scratch, not a branch
modification or a claim to verify that worker's display-routing behavior.
The final source/test additions also merged against that branch using `diff3`
in ignored build scratch, with all exit statuses 0 (M)[AC]:

```text
src/main.c: three-way text merge exit 0
tests/ipc_test.c: three-way text merge exit 0
tests/editor_p4_test.c: three-way text merge exit 0
```

## Missing and out of scope

LeakSanitizer cannot run in the sandbox. Sanitizer tests and fuzzing use
`ASAN_OPTIONS=detect_leaks=0`; the coordinator must rerun with leaks enabled.
No named implementation work is missing.
The final full sanitizer rerun after the test-only endpoint-probe refinement is
green, as are the earlier full suite and final release IPC suite. All requested
leak-disabled validation is complete.
Display-specific routing remains `edit-og2`; unrelated editor
capacity/index repair and Enter/brace indentation remain their assigned beads.
No out-of-scope production fix was made.
