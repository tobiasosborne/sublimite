# Editor status — P4.I / edit-457.16

Session 8 finisher (2026-10-10): the supplied WIP's missing-path fix/unique
fixture were already green. A controlled removal of only worker ENOENT fallback
reproduced the missing-path assertion; byte-for-byte restoration returned the
release suite to green. No additional production source change was needed.
Mandatory evidence: ../../docs/worker-reports/edit-457.16-s8.md;
current decisions: ../../docs/decisions/edit-457.16.md and P4.I.md.
Final gcc 13 make all and clang 18 ASan/UBSan make check exit 0 (49 test
binaries plus replay CLI). LeakSanitizer disabled; coordinator reruns leaks on.
Editor/tabs/ipc fuzz each exit 0 after 61 seconds (M)[AC], with
10002/73374/1517045 runs respectively. Release editor_test is also green.

Latest one-shot TRACK campaign (M)[AC], BAT0 Not charging, Xvfb :99,
load1=26.73: raster G3 ingress-to-T5 p50/p99 20.348306/42.608195 ms;
same-frame minimap p99 0.016307 ms. Second invocation p50/p99
6.218030/54.837236 ms, load1=27.01. References are G3 5.0/5.56 ms (G),
minimap p99 0.5 ms (G), invocation p99 10 ms (G). Loaded-box timings do not
certify acceptance. The release suite counts 0 allocations over 10000 mixed
keys with 100 tabs and IPC per backend (M)[AC], active counting guard.

Inherited amended-contract gaps, unchanged under NO NEW SCOPE: primary and
isolated launcher --wait; disconnected wait-token sweeping; minimap cached
worker publication and approximate byte drag; no-mutation indent LIMIT when
selection deletion has already succeeded. Full details and final verification
are in the session 8 report. The following older measurements are historical.

Rebase finisher (2026-10-09): missing paths now become new named buffers after
the worker's FILE_ERR_IO/ENOENT completion, using file_errno rather than the
UI thread's errno. Unique-path null/raster regressions cover initial bytes,
wrap defaults, failed-request rollback and subsequent missing-path acceptance.
Main's aligned editor allocation and poll_fd/drag_tab initialization are intact.

Latest loop measurements (M)[AC], BAT0=Not charging, load1=2.56, Xvfb :99:
100-tab raster G3 input-to-T5 p50/p99 4.718/9.993 ms against 5.0/5.56 ms (G),
so p99 acceptance remains open. Same-frame minimap p99 0.022 ms passes its
0.5 ms budget (G); actual second invocation p99 3.951 ms passes 10 ms (G).
The G3 benchmark now uses T5 after the fence; previous T4 rows cannot certify
G3. No backend tuning or quiet-box retry. Final evidence/report is in P4.I.md.
Primary/isolated --wait launcher integration newly described by main's P4.9
contract remains an inherited gap; secondary --wait is tested. No new scope.

Implemented: keys lookup/chords with counted unimplemented actions; stable
per-buffer trees/noncopied undo/history, MRU held/released/reversed, close/reopen,
modified markers, pointer reorder and top strip; detected Enter/brace indentation
in explicit undo groups; bracket/trailing-whitespace paint; same-frame minimap,
staleness marker and pointer scrolling; per-file wrap/visual affinity and current
geometry restoration; argv/stdin/single-instance IPC, poll integration, positional
opens and all-member --wait closure. No standalone keys/tabs/indent/minimap/ipc
source/header changes. Layout changes are paint additions only.

Source/contract choices and full evidence: ../../docs/decisions/P4.I.md.
P3.3/rename history: ../../docs/decisions/zzj.3.md and rename-sublimite.md.

Ownership: window arenas reserve grid/wrap/paint/input scratch; buffer arenas
reserve piece storage, history and minimap rows. Switch only binds stable handles
and redraws. Retained closed buffers keep view/log/history; minimap is finalized
and reinitialized without allocating. Evictions retire after rebinding; workers
release their snapshots before storage disappears. Default capacity is 128 live
and 16 retained closed tabs. Empty sets have an independent reserved viewport.

Input mutation never allocates libc memory. IPC setup is deferred until queued
keys and the pending key frame have reached submit; a delayed-backend socket test
proves that barrier and frame attribution. Only platform/present IO is excluded
from the counting guard during a key. Bounded index maintenance allocates nothing.

Partial-open failure frees all prepared resources before publication. Coordinates
use one-based display columns with certified clusters and visible scroll. More
than 64 KiB of required coordinate inspection returns CAPACITY / IPC_LIMIT before
opening any tab. Enter reserves 512 KiB scratch; capacity rejection is explicit.
Selection/dedent/insertion fits one undo group and reserves three journal ops;
backpressure submits before consuming another key. Successful mutation prefixes
are closed/journaled and a negative mutation error stops further loop turns.

Verification commands (all display-backed runs must remain on :99):

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 make all
DISPLAY=:99 EDIT_DISPLAY=:99 build/tests/editor_p4_test
DISPLAY=:99 EDIT_DISPLAY=:99 build/tests/editor_test
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make check
make fuzz
ASAN_OPTIONS=detect_leaks=0 build/fuzz/editor_fuzz -max_total_time=60 -max_len=1024
DISPLAY=:99 EDIT_DISPLAY=:99 build/bench/editor_bench --track --p4-only
```

Use approved execution outside the network sandbox for Xvfb/Unix socket tests;
:99 already exists but its socket is hidden in that sandbox. Read BAT0 status and
load1 before measurements. Leak-on checking belongs to the coordinator; the
release allocator guard is active, ASan's guard is explicitly inert. Do not
regenerate /tmp/edit-corpus. Worker benchmark evidence is TRACK only.

Current green release suites cover null/raster indentation, selection/CRLF undo,
EOF/clipped bracket and cursor-tab whitespace paint, complete queue journal
backpressure, counted redo-or-repeat no-op, MRU/reopen/reorder/dirty state,
per-tab wrap/soft affinity and gutter/resize geometry, minimap drag, interrupted
index recovery, actual socket/path/binary stdin/wait/rollback behavior, positional
visibility and cropped UTF-8 rejection, plus zero allocations over 10000 mixed
key presses with 100 tabs and IPC enabled. The original editor_test is also green.

The added G3 row explicitly selects 15 px and asserts physical 2880 x 1800.
Initial incorrectly scaled new G3 samples are withdrawn. The pre-existing G1/G11
rows still use the 30 px geometry getter with a 15 px editor; their A-size claim
needs a separate fix, outside this add-rows bead. No quiet-box retries were made.

Remaining limits / separate work: gate verdicts for raster G3/IPC and inherited
raster G11; view End on an empty final line when wrap is off; synchronous large
file attach/exact-count warmup; giant-query preemption and journal replay/staging
bounds; file save/recovery/config/clipboard/find UI; Unicode atlas residency.
Titles use ASCII replacement/background for nonresident glyphs. Deep no-wrap
bracket decorations omit unresolved positions, and cropped trailing runs obey
indent's window contract. Layout CRLF/4 GiB fixes are not included.
