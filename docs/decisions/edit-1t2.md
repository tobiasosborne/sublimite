# edit-1t2 — notification waits and the display-lock self-test

File notification tests must keep dispatching the shared inotify service and
decoding the worker mailbox until every expected file reports changed. Each
expected member is polled to retry its own coalesced/refused identity check,
including after the shared descriptor has been drained. A readable
inotify descriptor does not identify the target event: dispatch consumes only a
bounded batch, and a pending identity check can require a later pump. The wait
uses a monotonic deadline and polls both descriptors. Its watchdog and retry
cadence reuse `wait_msg`: (E) the existing asynchronous-file allowance of
5000 attempts × 1 ms = 5 s; the cadence also retries temporarily refused work.
These are test watchdogs, not latency requirements. No synchronous `file_check`
substitutes for notification delivery.

The regression queues unrelated parent-directory events ahead of the target
write. Its size derives from the notification API's batch: (P) more than
4096 bytes, using `sizeof(struct inotify_event)` as the minimum event size.
Alternating modify/close-write records prevent adjacent-event coalescing.

The display-lock contract is a tooling self-test. Its initial holder waits for
(E) ceil(107.69 s) = 108 s, derived from the longest recorded passing display
suite, `editor_test`, (M)[AC] in
[edit-qct-s7](../worker-reports/edit-qct-s7.md), “Other suites”. If another
process retains the lane beyond that allowance, print the wrapper diagnostic
and an explicit skip, then succeed. Other acquisition failures remain failures.
Run the nested-descriptor and exit-propagation checks inside the holder before
it releases ownership, avoiding another contention window between checks.

Changes belong only to tests and the contract driver; the file module and the
shared display-lock wrapper retain their existing behavior.
