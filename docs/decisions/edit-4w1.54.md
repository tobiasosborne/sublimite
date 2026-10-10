# edit-4w1.54 — session 8 journal continuation decisions

Scope is P1.9-2 §§1,4,6–14. Earlier implementation and red/green evidence
remain in [P1.9h.md](P1.9h.md). This continuation preserves those fixes and
adds journal-side prerequisites for §§4 and 7. It does not close §§4,7,8.
Editor/main findings §§2,3,5 belong to edit-zzj.13 and were not changed.

## Caller-driven protected INSERT steps

`journal_insert_step` retains the existing synchronous page-cache contract,
but encodes/copies/checksums and attempts only one record per call. The payload
ceiling is 16 KiB (G), reduced for a smaller configured batch or sync budget.
The caller keeps the immutable input and a byte progress cursor; no new
allocation, lock, worker submission or global state is needed. Each successful
step protects its prefix before returning. BUSY with advanced progress means
the caller can check input and resume; BUSY without progress requires a
durability fence or transport-gap repair. The caller may abandon the unaccepted
tail. This is deliberately a sequence of protected prefix mutations rather than
an atomic reservation of the entire paste. The legacy `journal_insert` retains
its existing preflight/whole-input IO-retention contract.

On IO the cursor includes the RAM-retained record, even after a short write.
Callers suspend completion, drain/retry off path and never append that record
again. FULL/previous IO/refused admission advance no cursor. Zero length and an
already complete cursor are no-ops; invalid offsets/cursors are rejected.

This byte/attempt ceiling bounds encoding work between caller input checks.
A Linux regular-file pwrite can still stall. The editor must use the new API
and apply/publish the corresponding protected prefix in its continuation;
the module alone cannot assert an unconditional UI wall-time bound.

## Complete durable-prefix credit

Stats now expose `durable_bytes` and `unprotected_bytes`. The former advances
only after a successful data fence through COMPLETE records, including PAD;
the latter is the actual UI-written file extent beyond that conservative
prefix. Newer UI writes are preserved when older worker stats are received.
A sync ending inside a large CRC-protected record releases no credit for that
record's partial prefix. A failed checkpoint-name barrier exposes no durable
bytes until directory retry succeeds.

Each INSERT step reserves its eventual PAD and returns BUSY before admitting
a record that would exceed configured sync_bytes beyond the received durable
prefix. This remains conservative if a kernel fence also covers concurrent
UI writes in the other batch. The delayed-bulk durable-image test shows no
admission beyond the default 64 KiB (G) and successful continuation after a
fence. Credit also works at the smallest 4 KiB (G) sync budget, and disk
exhaustion still reports sticky FULL.

This is an admission guarantee for the continuation API. Legacy INSERT and
other append APIs retain their current compatibility contract and can exceed
that volume. A complete default guarantee requires the editor to use bounded
protected continuations for every mutation and work to reserve independent
durability execution. A stalled device/worker still prevents a hard elapsed
time guarantee. The PRD maximum has not been weakened to a cadence target.

## Save preparation remains an integration obligation

The current standalone savectl performs legacy prepare/finish on a worker,
using an exclusively leased journal and a distinct transport pool. Its caller
defers journal edits during the lease, and the current editor is not wired to
that controller. Moving only an entry point would therefore leave later edits
unprotected or associate the wrong snapshot with the saved cutoff. No journal
lease/async API has been added without that integration. §8 remains open:
capture the immutable snapshot at enqueue, keep later protected deltas, perform
retention/checkpoint IO off the UI, and adopt through work mailboxes before
authorizing replacement. Existing retention mailbox/full-metadata semantics
remain enforced by their regression tests.

## Evidence

All session 8 runs use DISPLAY=:99 and EDIT_DISPLAY=:99. Power was Not charging
[AC]. Measurements are from the loaded shared box; no timing is a gate verdict.
The required worker report contains exact red/green output and final checks:
[edit-4w1.54-s8.md](../worker-reports/edit-4w1.54-s8.md).

The one paired benchmark alternated adjacent variants, reversing the initial
variant each pair, with independent exact-content replay and active malloc
checks. For a 1 MB (G fixture) paste, one-shot enqueue p50/p99 was
0.864/1.063 ms (M)[AC], and continuation enqueue-call sums were
1.391/1.935 ms (M)[AC]. Individual continuation calls p50/p99 were
17.488/56.857 us (M)[AC]. Continuation protected completion, including the
fixture's blocking off-path fence waits, was 34.823/40.195 ms (M)[AC]. Launch
load1 was 15.86 (M)[AC]; row load1 was 15.56 (M)[AC]. The test fixture has
input-check opportunities between calls; it does not run an editor event loop
or establish G1/G9. The cost of durability credit must be accounted for in that
integration. No repeat timing run or quiet-box wait was performed.
