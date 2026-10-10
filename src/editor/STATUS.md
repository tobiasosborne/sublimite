# Editor review continuation — edit-zzj.13 session 9

Done: editor-1 findings 10, 15, 17. Finding 22 adopts main's atomic undo
checkpoint behavior and adds an editor allocator-failure/tree/view/journal
regression. Finding 11 now measures complete action/staging, resize/blink and
submit intervals, but its hard preemption requirement remains open.

Remaining: 8, 9, 11, 13, 16, 21. Prefix publication and automatic burst grouping
have opt-in red reproductions. No private undo record encoding is imported into
the editor, no dependency module is changed, and no new globals are added.
Finding 14 was withdrawn before this continuation.

The forced release build and both full sanitizer checks pass: 59 binaries plus
replay CLI (M)[AC], LeakSanitizer disabled. Final queued-journal/unique-fixture
checks and the allocator failure/visible-grid replay sweep pass. The backward
environment clock change caused Make timestamp warnings; requested release,
sanitizer editor and fuzz targets were forcibly rebuilt. The complete final
forced sanitizer editor suite passes, including active/queued journal jobs and
visible-grid replay assertions. Editor fuzz passed
for 61 seconds (M)[AC] against 60 seconds
(G), with 16114 runs (M)[AC]. The release editor suite's active guards report
zero allocations over its typing workloads (M)[AC]. Both paired allocator
TRACK variants reach the existing G1 settle deadline at the published 0.9-lines
position on log_1g; no latency verdict is claimed.

Complete scope, red/green and final verification evidence:
[session 9 report](../../docs/worker-reports/edit-zzj.13-s9.md).
Design: [decision record](../../docs/decisions/edit-zzj.13.md).

The session 8 integration record is retained below.

# edit-457.10b session 9 — large-file port

Per-buffer large-file state, bounded mapped open, estimated/exact index swap,
worker warm/find/save and owner-routed completions are implemented. Warm snapshots
are reserved at open and queued after index publication. Bound mailbox receivers
cover prepared IPC buffers before tab installation; teardown cancels/joins before
reclaiming the arena. No new globals or dependency module changes.

Release and forced ASan large-file contracts are green. Final forced all/check
exited zero: gcc 13 release, clang 18 ASan/UBSan, 60 test binaries and replay CLI. Final editor module fuzz is clean for 61 s (M)[AC], 2870 runs (M)[AC], against
60 s (G); leaks disabled, coordinator reruns leaks on. DISPLAY/EDIT_DISPLAY :99.
Loaded large-file benches and a back-to-back warm-order comparison are recorded;
no timing gate pass is claimed. The separate edit-czn far-line typing stall
persists in both original main logic and the port and is not fixed here.

Complete per-finding red/green, measurements and missing acceptance items:
../../docs/worker-reports/edit-457.10b-s9.md. Decisions:
../../docs/decisions/edit-457.10b.md. Earlier editor status follows unchanged.

# Editor review continuation — edit-zzj.13 session 8

Current review work and red/green evidence are in
[the worker report](../../docs/worker-reports/edit-zzj.13-s8.md) and
[the decision record](../../docs/decisions/edit-zzj.13.md).

Completed/adopted: editor-1 blockers 1–7; P1.9-2 sections 2, 3 and 5;
WM_DELETE_WINDOW flushed acknowledgement; editor-1 12 and 23.
Main already supplies 1/2/6; controlled-fault regressions prove those fixes.
Failure recovery retains unaccepted staging and builds a fresh current-state
checkpoint only at explicit flush/exit. Backend init uses work mailboxes.
Long clipped lines reseed each subsequent logical row from exact piece queries.

Remain/unverified: editor-1 8–11, 13–17, 21 and 22. This bead stays open.
The inherited burst-grouping reproduction is opt-in and still red. Source
change detection, whole-file setup/slices, allocator recycling, idle journal
polling and native guard attribution are not addressed. Newer undo rollback
may alter 22, but an editor injected-failure test is still missing.

Final gcc make all is green. Final gcc make all and clang ASan/UBSan make check exit 0,
including 49 test binaries and replay CLI. The fairness draft is withdrawn.
Final release editor and P4.I sanitizer suites are green; active release guard
counts 0 allocations across 10000 keys per backend (M)[AC]. Final
editor fuzz timed run is clean for 61 s with 41432 runs (M)[AC], against 60 s (G).
A separate campaign found an inherited preferred-column view/model mismatch;
its exact reproducer and legacy-row confirmation are preserved in the report.
LeakSanitizer disabled; coordinator must rerun with leaks enabled.

The following P4.I status is retained as the rebased integration record.

# Editor status — P4.I / edit-457.16

edit-zzj.15 session 8: production WIP retained after rebase. Strengthened
default-selection fallback tests cover injected RENDER_ERR_INIT, real dlopen
failure, exact error propagation when policy is disabled, and one diagnostic
captured through typing/close. Controlled removal of selection/fallback/GPU
polling and a duplicate-log mutation reproduce red; retained implementation
is green. Release editor_test passes with active zero-allocation guards on
both selections; Xvfb EGL selection explicitly falls back to cpu-raster.
Final gcc 13 make all and clang 18 ASan/UBSan make check exit 0 (M)[AC],
49 sanitizer binaries plus replay CLI (M)[AC]. LeakSanitizer disabled;
coordinator reruns leaks on. Editor fuzz ran 61 s, 16111 executions (M)[AC]
for the requested 60 s budget (G), exit 0. Release GL suite also passes,
including active snapshot-submit allocation checks and native lifecycle skip.
G11 rows ran back to back with no after-idle/unfocused background wakes;
focused raster blink costs/wakes remain an inherited separate issue. G1's
long-corpus null reference hit its settle deadline before either native row;
main-source bench/editor-loop diagnostic reproduces that deadline.
no native EGL or G1 gate verdict is claimed. Mandatory session evidence and
current decisions: ../../docs/worker-reports/edit-zzj.15-s8.md and
../../docs/decisions/edit-zzj.15.md. The P4.I status below is preserved.

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
Implemented: public editor API and native/injected event loop, backend
selection in `src/main.c`, fixed reservations, piece/undo mutations, view
movement and repair, dirty layout slices, queue depth one/coalescing, deferred
resize, trace endpoints, mutation journal staging/pumping, negative stopped-loop
errors, blink timeout/unfocus policy. No dependency module changes.

edit-zzj.15: EGL is the application default; EDIT_BACKEND=gl|raster overrides
selection. GPU init failure logs its reason/code once and initializes raster
using fresh state and reserved workers; no loop retry. EGL Present callbacks
and bounded outstanding-frame polls are wired into the existing loop. No GL
poll/retry timer remains when the backend is inactive. Release allocation and
idle fixtures cover both selections, explicitly reporting raster fallback on
Xvfb. G1/G11 bench rows retain null/raster and add accurately labeled GL rows;
--require-gl prevents fallback from passing the native coordinator check.
Design, red/green, scoped limitations and exact real-display verification:
../../docs/decisions/zzj.15.md. Native GL allocation/idle requires that coordinator
check; GL renderer fixes remain edit-e6x.26. P4.I wiring is otherwise preserved.

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

Fuzz-discovered scrolled-undo RED, focused test written before its fix:

```text
editor_test:36: FAIL rc == EDITOR_OK || rc == EDITOR_MORE
editor_test:214: FAIL press(e, key('z', PLAT_MOD_CTRL, NULL)) == 0
editor_test:222: FAIL scrolled_undo() == 0
```

GREEN includes the scrolled-undo regression, byte/journal model, real release
guard over 10000 keys (P) with each backend, idle timeout/unfocus, delayed backend
queue depth/coalescing, native XKB translation/undo/redo/WM close, and an injected
piece allocator exhaustion that must stop with a negative error.

G1 (M)[AC], load1 3.39, shared-box TRACK, 10000 measured keys (P) per row:

| Backend | Drain to submit p50/p99 | Drain to T4 p50/p99 | Allocations |
|---|---:|---:|---:|
| null | 0.076/0.121 ms | 0.077/0.122 ms | 0 |
| raster | 0.227/0.405 ms | 0.355/0.987 ms | 0 |

G1 limits: 1.0/2.0 ms (G). Both rows type at published line 8053057 (P, floor
of 0.9 times fixture lines), byte 966366840 (M)[AC], of the existing 1 GiB
`/tmp/edit-corpus/log_1g.txt`. Every sampled key has its own containing frame.
The alternating insert/backspace workload is declared in benchmark output.
The coordinator owns the quiet verdict.

Corrected G11 (M)[AC], shared-box TRACK:

```text
null: load1=3.40 process CPU p50/p99=72962/129718 ns; UI polls=20; idle=0 unfocused=0
raster: load1=3.57 process CPU p50/p99=649600/1017413 ns; UI polls=100; idle=0 unfocused=0
```

Limits: process CPU 100000/200000 ns (G), wakeups <=2/s while blinking and zero
after 10 seconds or unfocus (G). Null is within the limits; raster misses CPU
and UI wakeups, before counting its additional worker polls. This acceptance
criterion remains open. No repeated G1 runs were made to seek a quiet box.

Remaining: raster G11; hard slice preemption for giant view/undo commands (needs
dependency API work); automatic post-edit index rebuilding; saved-file/journal
transaction integration and recovery UI beyond M0. The CLI preserves a unique
XDG data-directory journal and reports its path; Ctrl+S is intentionally omitted while the
save transaction dependency is in flight. Home/End are omitted after finding
the view EOF/End bug recorded in the decision. No out-of-scope module was fixed.

The burst allocation guard sums every mutation/layout/submit segment, pausing
only around platform/present/completion IO through `editor_config.on_io`.
This prevents XCB packets in a later turn from contaminating the process-wide
guard, per the frozen P2.0 library exemption. A late pre-fix release run at
load1 12.79 (M)[AC] exposed that measurement error. Final release GREEN
(M)[AC], BAT0=Full, load1 5.71:

```text
editor_test: scrolled undo/redo viewport repair passed
editor_test: mutation error is negative and stops the loop passed
editor_test: null script and journal byte model passed
editor_test: null 10000 keys mallocs=0 guard=active
editor_test: raster script and journal byte model passed
editor_test: raster 10000 keys mallocs=0 guard=active
editor_test: idle blink timeout and unfocused timer disarm passed
editor_test: queue depth one and coalesced edits passed
editor_test: native X11 translation, editor loop and WM close passed
editor_test: all passed
```

Build: `make all` exit 0 (gcc, strict warnings); `make fuzz` exit 0:

```text
fuzz: 15 fuzzers built
```

ASan/UBSan's editor suite passes with the counting guard explicitly ASan-inert.
Final full checks (M)[AC], BAT0=Full, load1 5.71, `detect_leaks=0`:

```text
make: Nothing to be done for 'all'.
check: 26 test binaries passed
test_replay_cli: all passed
fuzz: 15 fuzzers built
```

All three required make targets returned exit 0. `make bench` was not run over
unrelated modules; the editor's G1 and corrected G11 rows are recorded above.

Successful fuzz evidence (M)[AC], after the scrolled-undo fix, BAT0=Charging,
load1 6.16:

```text
#12554 DONE cov: 6266 ft: 37504 corp: 722/257Kb lim: 512 exec/s: 41 rss: 296Mb
Done 12554 runs in 301 second(s)
```

Final IO-boundary implementation, (M)[AC], BAT0=Full, load1 5.71:

```text
#4270 DONE cov: 6295 ft: 37902 corp: 675/235Kb lim: 530 exec/s: 70 rss: 290Mb
Done 4270 runs in 61 second(s)
```

Fuzz uses a full-copy byte/history model with Unicode one-shot segmentation,
independent of view's resumable scanner and returned mutation ranges. It checks
bytes and selection after each key/event. Cases include malformed bytes,
combining/ZWJ sequences, newline joins, Shift movement/replacement, vertical
movement, undo/redo invalidation, focus, resize, expose and ignored releases.
No findings in the successful runs. The original failed Unicode seeds exposed
the out-of-scope End issue and the fixed scrolled-undo integration bug; no
dependency source was changed.

Actual CLI smoke on `:99`: native `abc`, Backspace, Enter, `x`, WM close; exit 0,
then a separate journal replay verifies `ab\nx` exactly. Its scratch file and
journal were created only under `/tmp`.

Runtime rename (edit-457.18): CLI/binary `sublimite`, file and untitled windows
use sublimité. `editor_runtime_display` honours the explicit real-display
opt-in; pure tests pass literals and all live runs use :99. `editor_config_dir`
resolves the XDG config directory (the config parser remains unimplemented).
Main creates journals in the XDG sublimité data directory and writes
EDIT_TRACE_DUMP after editor_close joins workers. Verify with runtime_test,
cli_test, editor_test and tools/test_runtime_identity.sh after make all, then
make check with DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0.
Design/evidence: ../../docs/decisions/rename-sublimite.md.
Final rename verification: gcc make all passed; clang ASan/UBSan make check
passed with leaks disabled; make fuzz built all fuzzers. Focused identity and
isolated desktop-install contracts passed. Complete red/green and stamped
fuzz results are in the decision document; no hot-path bench was rerun.

## edit-e6x.28 session 8 — minimal WM close overlap

CLOSE now latches quit outside command storage and returns CLOSED after native
pumping before view/layout/submit continuation. The :99 regression holds a
frame and busy view, fills input storage, requires same-turn close, and checks
flushed staged-edit replay. Main's flush-before-close sequence is retained.
Urgent close preserves applied edits but does not drain queued commands;
coordinate this limitation with edit-zzj.13's pending-input review work.
Release editor allocation guards still report zero typing allocations; editor
and X11 fuzzers ran clean for the required duration, leaks disabled.
Evidence and remaining full-suite/hardware limitations are in
docs/worker-reports/edit-e6x.28-s8.md; design in docs/decisions/edit-e6x.28.md.

Final s8: make all/check/fuzz exited zero; 51 sanitizer test binaries plus
replay CLI passed, leaks disabled; editor fuzz ran 61 seconds clean (M)[AC].
The initial unrelated file/raster shutdown failure remains in the report.

Amendment (edit-457.18): file and untitled windows use lower-case `sublimité`;
cli_test and editor_test pass the amended title assertions. main.c is unchanged:
EDIT_ALLOW_REAL_DISPLAY selection and EDIT_TRACE_DUMP remain intact. No
remaining identity work; the existing config-parser limitation remains.
Amendment red/green and verification: ../../docs/decisions/rename-sublimite.md.
Amendment final verification: gcc make all, clang ASan/UBSan make check
(detect_leaks=0), make fuzz and the focused contracts passed; no new open
problem. Stamped X11 fuzz smoke evidence is in the decision document.

P3.6 first-frame link-order experiment (edit-zzj.7): common startup benchmark,
profile trace/linker order and three isolated candidates are complete under
variants/P3.6. Production sources and Makefile are unchanged. One stamped,
interleaved 200-launch-per-candidate warm Xvfb :99 series found lower median
code RSS for ordered code but inconclusive paired latency. Recommendation:
reject adoption now; no winner.patch. Cold-cache and real-display effects were
not measured. Decision, Pareto tables, exact endpoint/page interpretation,
red/green and verification commands: ../../docs/decisions/P3.6.md.
Verify with tools/linkorder_bench.sh selftest and
variants/P3.6/common/contract.sh. Full GCC release build, clang ASan/UBSan
check (41 test binaries, leaks disabled), and fuzz build (21 fuzzers) passed. Remaining limitations are
recorded in the decision.

## edit-zzj.12 session 9 — raster G11 caret path

Prepared blink/unfocus paint now changes only the caret cell or wide pair,
without strip/fence worker submissions. Raster uses inline SSE2, cell-sized
XShm uploads and a bounded Present update region. Genuine completion is
observed on existing loop turns; waiting damage arms the private X fd in the
existing epoll set. No completion polling timer is added. General fence-job
work and teardown are byte-for-byte unchanged. The benchmark's undamaged
settle endpoint is T4; T5 rows keep an explicit readiness wait.

Focused worker-lease, selection/wide/wrap, scalar pixel and delayed-Present
regressions are green. GCC 13 forced full release build passes. Editor/raster
fuzz completed cleanly for at least 60 seconds (G), leaks disabled. Final
loaded-box G11 (M)[AC]: raster CPU p50/p99 0.109487/0.126305 ms, nineteen inline
frames with zero worker jobs, twenty UI polls and no after-idle/unfocused wakes.
The 0.1 ms median gate (G) still misses; no acceptance/closure is claimed.
Final clang 18 ASan/UBSan make check exits zero: 59 test binaries plus replay
CLI, leaks disabled (M)[AC]. The unrelated transient view assertion, clock-skew
handling, pasted red/green and complete limitations are in
../../docs/worker-reports/edit-zzj.12-s9.md. Design: ../../docs/decisions/edit-zzj.12.md.
