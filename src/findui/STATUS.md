# edit-lez — P4-modules-2 §§21–30, session 9

Incomplete bead. Implemented literal ASCII folding, persistent/SIMD literal
word counting, bounded word publication, resumable mailbox delivery, retained
viewport count/selection, independent first-offset cache, paged replacement,
byte-sliced large-match deletion, host mutation adapter and full source leases.

Remaining: generic regex whole-word visitor; bounded multi-byte/regex viewport
scans and immediate window priority; actual editor mutation binding; editor
buffer eviction/reload and independent save-storage leases. Page fetching
rescans the immutable source. Regex undo admission is conservative. Host and
lease APIs require coordinator integration; legacy standalone APIs remain.

Red/green transcripts, loaded TRACK comparisons and final verification status:
[mandatory worker report](../../docs/worker-reports/edit-lez-s9.md).
Design/lifetime contract: [decision](../../docs/decisions/edit-lez.md).
Final GCC 13 make all and Clang 18 ASan/UBSan make check exited 0:
59 sanitizer binaries plus replay CLI passed, using Xvfb :99 only and
ASAN_OPTIONS=detect_leaks=0. Final findui fuzz: (M)[AC]14992 runs in
(M)[AC]61 seconds, clean. Release typing allocator guard and diff checks pass.
Coordinator must rerun LeakSanitizer enabled and keep partial findings open.
No new globals, typing allocation, frozen find-header or editor-internal edits.

---

# edit-4w1.58 — P1-1 §§7–11, session 8

Completed bounded dense popcount counting, core visitor integration for the
panel, shared regex retry limits and rejecting/supervised find benchmarks.
Full details and runtime red/green evidence:
[worker report](../../docs/worker-reports/edit-4w1.58-s8.md);
[design decisions](../../docs/decisions/edit-4w1.58.md).

Both module fuzz targets completed cleanly for the requested duration. Release
module tests and the active editor typing allocator guard passed. Final GCC 13
make all and Clang 18 ASan/UBSan make check both exited 0:
51 sanitizer binaries plus replay CLI passed. Invocations used Xvfb :99 and
ASAN_OPTIONS=detect_leaks=0. Successful supervised fixtures preserve normal
exit/leak-check hooks; timed-out fixtures retain arguments until termination.
Coordinator: rerun with leaks enabled; supply the absent official all-a corpus
fixture for the official G6 matrix. Loaded measurements are TRACK comparisons.
The frozen find public header and its semantic assertions remain intact; test
fixture execution is now supervised. The regex engine returns FIND_ERR_LIMIT
on its documented request-wide state/candidate budget; a streaming replacement
is deferred. Whole-word filtering keeps the existing candidate/overlap policy.

# findui — P4.6 / edit-457.6

Standalone panel module implemented. Public API: `findui.h`. Design and wiring
contract: `docs/decisions/P4.6.md`.

Done: arena-reserved state/storage; per-query-edit cancellation and generation
filtering; worker-only P1.10 searches, ASCII case/word/regex toggles; inline
mailbox streaming/backpressure; first-match and late visible-window caches;
wrap-around plus worker fetching of uncached ordinals; literal replacement
one/all in a sliced explicit undo group; own bottom-row rendering and damage;
unit tests, independent-model fuzzer, stamped G6c corpus benchmark.

Integration remains with the editor wiring bead. No editor/main/find/undo/layout
or frozen header was edited. The contract proposes work retirement, editor
binding/grid, layout byte overlays/window and undo admission/transaction
accessors. The current work retirement helper reads documented slot atomics;
undo is used through its existing public functions only.

Limits: byte/ASCII semantics; no capture expansion; compiled case-insensitive
literals inherit core regex limits. Highlight and replacement overflow are
explicit. Replace-all requires all ranges to fit the configured cache/history
budget. A replace allocation failure preserves an undoable successful prefix;
atomic command rollback awaits an undo transaction API. The host must supply
correct document versions, matching tree/log and a guaranteed undo record cap.

Verification complete: gcc `make all` passed; clang ASan/UBSan `make check`
passed (`check: 44 test binaries passed`, replay CLI passed); `make fuzz` built
23 fuzzers. Targeted gcc and sanitizer findui suites pass, including allocator
fault recovery and saturated-mailbox cancellation. The full display suite used
the existing host Xvfb :99 outside the isolated sandbox (its socket is invisible
inside); no temporary Xvfb was successfully started or left running.

Independent-model fuzz: (M)[AC] load1=5.89, 27580 runs/61 s initially;
(M)[AC] load1=9.03, 7825 runs/61 s after the empty-window regression fix.
No assertion or sanitizer failures. Fuzz seeds/logs are under
`/tmp/edit-457.6-fuzz-*` (temporary verification artifacts, no corpus changes).

One release corpus bench, TRACK only: (M)[AC] load1=10.71, 64 edits,
logical cancel ack p50/p99=0.002246/0.009399 ms versus (G)1/5 ms.
Calling-thread scan observations=(M)0. Full stamped lines and red-green evidence
are in P4.6.md. The coordinator owns gate verdicts and the leaks-enabled rerun.

Verify (safe display; this sandbox cannot run LeakSanitizer):

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 make -j4 all
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make -j4 check
DISPLAY=:99 EDIT_DISPLAY=:99 make -j4 fuzz
cat /sys/class/power_supply/BAT0/status
cut -d' ' -f1 /proc/loadavg
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/fuzz/findui_fuzz -max_total_time=60 -max_len=384
cat /sys/class/power_supply/BAT0/status
cut -d' ' -f1 /proc/loadavg
DISPLAY=:99 EDIT_DISPLAY=:99 build/bench/findui_bench --track
```

The coordinator reruns sanitizer checks with leaks enabled and owns gate verdicts.
Do not regenerate `/tmp/edit-corpus` or repeatedly run benches to chase low load.
