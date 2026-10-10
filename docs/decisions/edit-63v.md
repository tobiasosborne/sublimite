# edit-63v: CLI test window waiting and failure ownership

The CLI integration test owns its forked editor, XCB connection, property reply,
trace records, and open streams. Assertions now jump to cleanup; cleanup frees
each acquired resource and SIGKILLs/reaps any editor not already reaped. The main
test also closes streams/directories and removes its private temporary fixtures
on failure or skip. `waitpid` retries EINTR.

Window discovery polls every 10 ms (E), with a monotonic deadline of
`10 + 2 * floor(min(load1, 20))` seconds (E), reading load1 from `/proc/loadavg`.
The normal budget is therefore 10–50 seconds (E). Missing/unreadable load data
uses the minimum. The old process-wide 30-second alarm is removed because it
could interrupt cleanup and contradict the scaled deadline. Waiting for the
editor to exit after WM_DELETE_WINDOW also has a deadline and uses the same
cleanup on failure.

A missing window skips only when `waitpid(WNOHANG | WUNTRACED)` reports a running
child, `kill(child, 0)` succeeds, and XCB still has a healthy connection. Exited,
signaled, stopped, or unobservable children fail. Skip propagates separately
from success, so the journal/dump assertions are not attempted after skipping.
Normal sessions retain the window, title, exit-status, trace, and journal checks.

Test-only environment probes are confined to `tests/cli_test.c`:
`CLI_TEST_FORCE_WINDOW_TIMEOUT` bypasses window discovery with a zero budget;
`CLI_TEST_FORCE_ASSERT_FAILURE` fails with a live child and allocated title;
`CLI_TEST_CHILD_EXIT` exits the child with status 23;
`CLI_TEST_CHILD_STOP` stops it before startup. Any set value enables the probe.

Evidence and the unrelated full-suite gate failure are recorded in
`docs/worker-reports/edit-63v-s8.md`.
