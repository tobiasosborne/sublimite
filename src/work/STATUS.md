# edit-4w1.56 — session 8

P1-1 §4: optional foreground lane, independent capacity/mailbox, cooperative
continuations and lease-preserving priority are implemented. The editor and jump
bench enable the lane. Copied-file jumps prioritize the existing index build;
background and foreground index builds both yield bounded CPU batches.

Migration waits for old-worker result/callback reservations before its first
foreground invocation, preserving per-lease order across mailboxes. Other
foreground jobs can run during the handoff. Continuations and upgrades allocate
nothing and keep caller-owned arguments until physical completion. Blocking
bulk I/O retains its own execution resource and completion ownership.

Release work/editor allocation guards, work TSan, foreground real-client tests,
index span-bound/lease tests, benchmark self-checks, GCC all, and work/lineidx fuzz
campaigns pass. Final full ASan/UBSan make check passes all test binaries and replay CLI;
LeakSanitizer is disabled for this worker. The final seeded work fuzzer uses
two raster workers and validates handoff between all producer identities. The final full suite previously
exposed an unrelated raster fence/viewport teardown use-after-free; raster/file
code is untouched. Mapped/cold-source jumps need nonblocking I/O continuation
integration before using the CPU foreground lane. Dirty-index repair and
production find continuations are separate consumer work, not claimed here.

See `docs/decisions/edit-4w1.56.md` and
`docs/worker-reports/edit-4w1.56-s8.md` for the pasted red/green evidence and
remaining system coverage. New module fuzzer: `build/fuzz/work_fuzz`; seeds under
`/tmp/edit-corpus/work-fuzz`. Display only `:99`.

# Work module

P1.8d (edit-4w1.51 + edit-e6x.25) adds generation/lease-selective mailbox
receive, optional registered dispatch, physical lease-completion acknowledgement,
and atomic all-or-nothing batch enqueue. Lineidx adopts only sealed ranges
delivered by work validation; source retirement uses the public completion API.
Raster submits its entire strip batch atomically and has no failed-submit
waiting rollback. Existing work callers and signatures remain source-compatible.
See `docs/decisions/P1.8d.md` for contracts and red/green evidence.

Selective receive keeps foreign messages/reservations in their worker ring;
a foreign head continues to own ring capacity until its client/shared dispatcher
drains it. Registered handlers let existing ordinary drains safely deliver
owned results. Unbind before freeing receiver state. Slot completion identities
do not wrap within a pool lifetime; discard handles before reinitialization.
New APIs allocate nothing; the release guard covers batch, bind, receive and
completion query. Default tests include both formerly opt-in review probes.

Release probes, expanded work/lineidx checks, final GCC all/fuzz builds,
release allocation guard and final work TSan pass. Lineidx TSan and both fuzz
campaigns passed. Each affected module's single TRACK bench exited successfully;
benches and campaigns preceded the final dormant-cursor wrap guard, which is
covered by the default work suite and final TSan. Final full ASan/UBSan validation
after that guard passes, including live raster conformance and replay CLI.
No assigned implementation or test blocker remains. All evidence is
recorded in P1.8d. Verify new cases with release or sanitizer `work_test receive`,
`work_test batch`, `lineidx_test --review=13`, and `raster_test --review 4`.
Keep `DISPLAY=:99 EDIT_DISPLAY=:99`; sanitizer workers use
`ASAN_OPTIONS=detect_leaks=0`. Broader commands remain below.

P1.8c (edit-4w1.49) implements work-scan-1 §§1–8, 15, 17, 18. Fixed recursive
drain, heap caller alignment, epoch wrap, queued shutdown reservations,
inactive-pool submission, synchronization failure unwinding, bulk cancellation
capacity, drain slicing/continuation, and shutdown timestamps. The suite now
enforces live mailbox ordering and the release allocation contract. See
`docs/decisions/P1.8c.md` for semantic changes and each red/green line.

Pool storage must be aligned to `_Alignof(work_pool)`. All UI/lifecycle calls
are serialized on the UI thread; callbacks may submit/cancel, nested drains
return zero, and init/shutdown wait until callbacks finish. Handles belong
to a single initialized pool lifetime and must be discarded at shutdown.
Slots retire before epoch wrap. Queued arguments stay caller-owned. Running
jobs still must poll cancellation and retain their resources until return.

With raster workers, a reserved suffix guarantees a full raster batch remains
available under bulk saturation. Drains are bounded by examined-message count
and between-callback deadline; callers check input and resume while pending
or eventfd-ready. Individual callbacks must themselves be bounded. Shutdown
clears queues/busy reservations; stale mailbox reservations can be drained
afterwards.

Verify with `DISPLAY=:99 EDIT_DISPLAY=:99 make all`, release
`build/tests/work_test` (allocation guard must be active),
`ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 make check`, and
`DISPLAY=:99 EDIT_DISPLAY=:99 make fuzz`. At P1.8c there was no work-module fuzzer (session 8 adds the target above).
The randomized work state machine
and live mailbox tests can additionally run under GCC TSan (`-fno-pie -no-pie`)
using `TSAN_OPTIONS=halt_on_error=1 setarch x86_64 -R <binary> 15`. Tail/head
relaxed mutants are rejected by that driver; production passed.

The finishing run updates the authorized journal capacity assertion from
`WORK_MAX_JOBS-2` to `WORK_MAX_JOBS-WORK_RASTER_RESERVE-1`: the unrelated
raster job occupies the reserved suffix, while the unread journal completion
keeps one shared bulk slot reserved. Exact saturation, successful flush, and
preservation of the unrelated message remain enforced. The original assertion
failed at `journal_test:1069`; the corrected journal suite passed under
ASan/UBSan and GCC release, with the release allocation guard active. Full-suite
verification also exposed `raster_test:787`'s single-drain assumption. Its two
final collection steps now resume bounded drains while mailbox messages remain
after worker completion. Full-ring setup and all delivery/identity/order checks
remain intact. No implementation changes were needed. See §§7–8 in P1.8c.

Previous-run release work tests with the active allocation guard, ASan/UBSan
work tests, and full work suite under TSan passed. Finishing-run GCC `make all`,
complete ASan/UBSan `make check` including live raster conformance and replay
CLI, and `make fuzz` passed. Both original test failures and the final green
evidence are recorded in P1.8c; no finishing-run test blocker remains.
The display-dependent suite must reach Xvfb on `:99`; sandbox network isolation
hides its abstract Unix socket, so run that suite outside network isolation
with both display variables fixed to `:99`.

The one permitted work bench completed as TRACK (M)[AC], load1=9.61; numbers
and methodological limits are in P1.8c. No further bench run is needed here.
Remaining review findings outside the assigned sections are untouched;
work_bench is owned by P1.2c. Leak checking is deferred to the coordinator's
unsandboxed run.
