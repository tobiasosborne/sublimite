# edit-457.8 — hot exit session integration

Full VIEW records preserve selection, preferred column, logical/visual scroll,
wrap and cursor affinity while legacy VIEW records remain readable. Applied
input and cursor/tab changes journal before submission; raw window dimensions
restore before window/backend creation and replacement checkpoint publication.
Window size at restart: explicit launch size > journalled chosen size (WINDOW
v2, 12 bytes, chosen flag) > DPI default; legacy 8-byte records keep the default.
Save captures and failure checkpoints share the full encoding. Metadata uses
reserved storage and existing durability credit, with no typing allocation.

The requested crash trials pass (G: bead acceptance count; M[AC]: successful
completed loop count) with null rendering, seeded kill boundaries and held
Present, plus schema/save checks and the active release allocation guard.
GCC `make all` passes. Complete sanitizer suite audit, native/IPC sandbox
failures, LeakSanitizer restriction and stamped benchmark observations are in
[the worker report](../../docs/worker-reports/edit-457.8.md). The coordinator must
rerun permitted display/IPC/leak checks and the new native CLI crash trial;
Astra verification remains required before handoff.

[Decision record](../../docs/decisions/edit-457.8.md).

# edit-a6v — initial painted surface follows launch geometry

Explicit launch window pixels take precedence, per axis, over the journal's
WINDOW record. Since edit-457.8, recovery applies the window size directly
(install_session, before window/backend creation) instead of queueing a resize;
it uses the launch size where configured and clamps a journalled size to the
reserved surface maximum, so the created window, initial grid and submitted
frames keep the same size.
Native creation and raster reservations already honored launch geometry and
needed no changes. Later native resize still uses the existing path.

The headless null/CPU-capacity regression covers fresh startup, recovery from
the legacy default geometry, retained document bytes and subsequent resize.
Focused sanitizer/release red/green and the existing release typing allocation
guard pass; GCC make all passes. Full check stops at sandbox-denied IPC setup.
Individual headless suites pass except denied IPC and the mixed editor suite's
pre-existing reload/wait failure. Native setup failures, the x11 order fixture's
post-failure sanitizer crash and coordinator screenshot acceptance are recorded
in the [worker report](../../docs/worker-reports/edit-a6v.md).
Design: [edit-a6v](../../docs/decisions/edit-a6v.md).

# edit-d50 continuation — daily-use keys

The remaining daily-use executors now include file-language toggle comment,
go-to-line preview/accept/cancel, Ctrl+D first-word selection, Ctrl+K Ctrl+C
centering and Ctrl+K Ctrl+D single-selection literal skipping. The existing
indent/unindent, duplicate/delete/move/join, case, transpose and mark/kill/yank
fixtures still pass. New action and independent key-capability fixtures were
run red before implementation, then green in release and ASan/UBSan builds.

No typing-path malloc: plans reuse prepaid staging and bounded stack windows;
the editor owns prompt storage before input, and exact index seeks continue
through existing bounded calls. `KEYS_ARG_SINGLE_SELECTION` identifies partial
occurrence capabilities explicitly. Further Ctrl+D expansion, syntax reindent,
clipboard history UI, embedded/custom comment scopes, folding/diff/panes and
oversized edit/skip plans remain unsupported. Unknown/plain text has no comment
syntax. The full bead is still incomplete for complete Sublime parity.

`make all`, the focused release/sanitizer checks, the headless module sweep
and the view fuzzer corpus replay passed. Release guarded typing and command
fixtures allocate no libc storage. See the
[worker report](../../docs/worker-reports/edit-d50.md) and
[decision audit](../../docs/decisions/edit-d50.md) for exact evidence and limits.
Standalone IPC still fails with sandbox EPERM at socket initialization.
The coordinator must rerun native display/IPC/leak checks and the comparison
drive; no display suites were executed in this continuation.

---

# edit-d50 — default Linux keys (src:tobias)

Worker changes are buildable but **this bead is incomplete**. The decision
[audit](../../docs/decisions/edit-d50.md) records implemented fast paths,
remaining text/syntax/multi-selection capabilities and retained UI notices.
The [worker report](../../docs/worker-reports/edit-d50.md) contains the red/green
runs, release/sanitizer verification and sandbox socket failures. The coordinator
must rerun the display/IPC suites and compare native behavior with Sublime on
its safe display before closing or handing over this bead.

Typing-path storage is prepaid. New view plans use existing view scan/window
limits; oversized or approximate commands refuse without changing text,
selection or history. Accepted edits are explicit undo groups; undo preserves
trusted distant anchors, and soft-history eviction keeps text/selection ordering.

The older module status follows; the capability-flag audit above supersedes
older descriptions of `KEYS_ARG_UNIMPLEMENTED`.

---

# edit-gdj — separate Shift chord regression

The checked-in Astra review withdrew the original anchor-loss finding. Existing input/view code already retains the anchor across separate Shift chords. Added exact-range editor regressions for word, vertical, Home/End, document and page movement, reversal, modifier releases, and movement/mutation reset in wrapped and unwrapped views. The release allocation guard remains green. No production behavior changed.

Focused gcc and clang ASan/UBSan regressions pass; the deliberately injected anchor reset fails the new assertion, and input.c was restored byte-for-byte. GCC `make all` passes. Broader sandbox validation and coordinator reruns are recorded in the [worker report](../../docs/worker-reports/edit-gdj.md). The other keyboard beads were left for separate workers under the binding scope rule.

[Decision record](../../docs/decisions/edit-gdj.md).
# edit-m3g — display sizing and worker zoom

Startup selects Xft DPI or RANDR physical DPI, derives point-sized em pixels and
work-area window geometry, and honors EDIT_FONT_SIZE_PT. Ctrl+= / Ctrl+- / Ctrl+0
use coalesced foreground atlas jobs with INIT-reserved storage and backend frame
retirement before adoption. Explicit legacy pixel configurations retain their
existing font-height semantics. Font and native surface capacities follow the
existing renderer bounds.

Focused red/green and allocation/lifetime checks pass. Native verification and
the next Astra screenshot/latency drive remain with the coordinator because the
sandbox denies X listening/connection sockets. Full check and individual suite
outcomes, including independent IPC restrictions, are recorded in
[the worker report](../../docs/worker-reports/edit-m3g.md).
Design and numeric derivations: [edit-m3g](../../docs/decisions/edit-m3g.md).

# edit-cde — clipboard integration

Copy/cut/paste actions, alternate key bindings, selection-driven PRIMARY,
positioned middle-click paste, arena-backed native/null ownership and worker
normalization/capture are implemented. Replacement/cut share an explicit undo
group and bounded mutation/journal helpers. Physical ingress and paste frame
acknowledgement are preserved; queued PRIMARY and click viewport regressions
pass. Native allocator callbacks preserve borrowed and deferred ownership lifetimes; buffer retirement pins snapshots.

Clipboard acceptance is green under clang ASan/UBSan and gcc release with the
active malloc guard. The release tree builds. Full-check restrictions and exact
red/green evidence are recorded in [the worker report](../../docs/worker-reports/edit-cde.md).
Display/native transport, IPC-dependent suites and the next Astra comparison
must be rerun by the coordinator. No paint.c, large.c or save-path edits.
# edit-2ty — loaded large-file prefix replay

Implemented in this worktree: certified EOF/previous-page anchors in large.c,
nearby backward scans with exact index fallback, matching viewport/tree guards,
and pending-query cache completion for queued navigation. No input/paint/save
change, allocation on typing, new global or worker wait.

The release Astra sequence passes the existing EDITOR_SLICE_NS assertion in
nowrap and file-default wrap, with active allocation counting and byte/viewport
oracles. Sanitizer functional/trace cases pass; instrumented timings are TRACK,
not asserted as production performance. Full gcc make all passes. Full check
and mixed suites are blocked by X connections and AF_UNIX binds in the sandbox;
the worker report lists every failure and the independent successful suites.

Remaining: coordinator's leak-enabled sanitizer/display/IPC rerun outside the
sandbox on Xvfb :99, then Astra vs Sublime verification before handover. Early
unindexed input and arbitrary long-line fallback timing are not certified here.
Private view query tags are coupled to the pending-result bridge.

Evidence: [worker report](../../docs/worker-reports/edit-2ty.md).
Design: [decision](../../docs/decisions/edit-2ty.md).

# edit-ck4 save and startup continuation

Ctrl+S now captures a published immutable session root and enqueues savectl on
the bulk worker. Saving is composed on the UI thread and acknowledged only after
backend submission. Completion reconciles the journal and selected saved-history
boundary; post-cutoff edits stay dirty. Failures belong to the owning buffer.
Startup discovers unlocked session journals or replays an explicitly supplied
existing journal before the first text frame, then durably publishes recovered
state before retiring prior recovery artifacts.

The default save/startup probes and the headless editor_session_test cover disk
bytes, dirty state, repeated/new-target saves, concurrent edits, shared targets,
inactive failure, busy-backend Saving acknowledgement, and interrupted save
recovery before/after replacement. Red/green, required build results, sandbox
socket/display failures and remaining limitations are recorded in
[the worker report](../../docs/worker-reports/edit-ck4.md).

No edits to the other workers' x11/font/minimap/paint/large implementations.
Native display suites and the next Astra comparison remain coordinator work.
# edit-2ts — startup tracing and asynchronous GPU discovery

Raster bootstrap paints while a separate worker discovers EGL. Headless regressions cover held failing initialization, held worker finishing, successful adoption, typing allocation guards, and late eventfd notification drainage. The requested GPU is never joined by the painting loop. Native startup measurements and the next Astra drive remain coordinator work. Full check is blocked by display/Unix listeners and an unrelated reload/wait failure, recorded without an outside-scope repair.

Evidence, exact validation outcomes and remaining acceptance: [worker report](../../docs/worker-reports/edit-2ts.md).
Design: [decision record](../../docs/decisions/edit-2ts.md).
# edit-3kk — resident scroll and refresh host wiring

Logical wheel/page/document/follow/resize/source adoption is owned by scrollctl;
input.c retains wheel dispatch. Each buffer retains immutable backing until
resident workers physically retire. Warm wheel/typing follows certified layout
rows, avoiding a redundant source copy. Overscan is reserved and initialized
separately from the visible surface, including preview and resize transitions.
The renderer snapshots Q8 origin/clip/overscan. Null refresh observations retain
input/source/geometry/scroll/wrap/sidebar identity and expose repeat/missing/stale
opportunities without releasing a newer backend slot.

The null displayed probe passes and rejects the deliberately repeated stream.
Physical cadence remains unsupported with completion-only native observations;
exact wrapped metadata remains a separate host requirement. Native raster/GL
pixels and the comparison drive require the coordinator's Xvfb run outside the
sandbox. Unix IPC binding is denied here as well as the X socket. Full measured
red/green, sanitizer/build results and limitations are in
[the worker report](../../docs/worker-reports/edit-3kk.md).

Validation commands: `make all`; `ASAN_OPTIONS=detect_leaks=0 make check`;
`build/tests/editor_scroll_test`; `build/tests/raster_scroll_test`;
`build/tests/gl_scroll_test`; `build/tests/scroll_test --require-editor-scroll`;
`build/bench/scroll_bench --displayed-probe`. The release counting guard supplies
allocation evidence; ASan's guard is inert. The earlier module history follows.

Final validation: GCC `make all` passes; Clang ASan/UBSan builds all suites.
The required `make check` stops at denied CLI IPC. Individually, every suite
without display or restricted IPC passes; the exact manifest and hybrid-suite
failures are in the report. Focused scroll, raster pixels, GL snapshots, resize,
held-source cancellation, EOF typing, deep typing and index repair are green.
The active release typing guard passes. Native GL is explicitly skipped here.
Final model fuzzing stops at an unchanged pure-view/oracle preferred-column
policy disagreement after Right at EOF, reproduced without editor/scroll calls;
no out-of-scope view/oracle fix was made. Physical cadence and wrapped metadata
remain the stated integration limits. No comparison-drive hand-over is claimed.
# edit-73j — editor wiring

Mouse caret/drag/word/line selection, wheel per-line fallback with resident
logical navigation, find/replace controller and leases, cached worker minimap
publication, and adopted font runtime body/title lookup are implemented.
Distant wrapped/clipped matches have integrated visibility regressions.
Reservations are derived beside the code; typing uses no malloc.

Final gcc `make all`, release/ASan headless regression partitions, active
release allocator guard, unchanged-chrome regression and benchmark-harness
contracts pass. `make check` stops at denied Unix socket initialization;
X-dependent defaults also fail in the sandbox. The coordinator must rerun
native/leaks-on verification and the Astra drive before acceptance/closure.
Pixel origin/overscan/refresh certification and existing module limits are
not claimed. Adjacent findui history/page scan issues are reported, not fixed.

[Worker report](../../docs/worker-reports/edit-73j.md) contains the red/green
runs, all-suite attempt, bench observations, limitations and changed files.
[Decisions](../../docs/decisions/edit-73j.md).

# edit-ck4 — session 9

Partial P4-3 recovery integration: source-choice checkpoints, healthy metadata/target records, protected journal slices and a private durability worker. Save/session-root wiring and startup recovery remain open; pending COPY reload and asynchronous request preparation remain incomplete.

Evidence and remaining acceptance: [worker report](../../docs/worker-reports/edit-ck4-s9.md).
Design: [decision record](../../docs/decisions/edit-ck4.md). No new globals; leaks-disabled sanitizer verification requires coordinator rerun with leaks enabled.

# edit-9lf session 9 — P4-3 editor loop/tabs continuation

Implemented regressions/fixes: sections 2, 11, 12, 13, 14, 41, 40. Public tab
activation refuses sliced replay; reads use its committed snapshot. Deferred
file/adoption/save failures are owner-local. Pending COPY controls bypass parked
edits bound to ID/source generation. Stale tabs retain navigation/closure.
Recovery keys require a submitted prompt and fresh explicit ingress. Capture
owns stable identity; unchanged final chrome adds no tab-row damage.

Partial: section 16 retains tab/view/wait identity on explicit unjournalled
reload, but controller revision/silent reload and journal reconciliation remain.
Section 22 avoids UI teardown waits by retaining file storage until shutdown;
nonfile leases poll exact known handles/index retirement. File retirement API
and timely physical reclamation are missing. Retention can exhaust reservation,
so admission checks free storage before preparing/publishing requests.

Remaining: section 27. The dc4015e bounded undo query has group counts but no
record count/memory query. The protected-history-page replacement regression is
opt-in and remains red; undo source/header are unchanged (API-use-only scope).
No production private-state workaround or new globals. No large.c/open.c hunks
were edited. Final validation/evidence and limitations are recorded in
[the required worker report](../../docs/worker-reports/edit-9lf-s9.md), with
[design choices](../../docs/decisions/edit-9lf.md). This bead remains incomplete.

# edit-zzj.13b slice 3 / session 9

Implemented: automatic burst undo grouping (13), constant-time public undo
history query and editor ring accounting (9), mapped-source watch/focus/bulk
identity integration and stale-source suspension with minimal reload/keep (16),
and calibrated native core-key allocation attribution in the default suite (21).

Partial: pending copy-open preview/adoption (8) is default-tested, but mapped
acquisition still retains the existing OPEN_READY startup contract. Multi-record
undo/redo now uses sliced replay (11); indivisible mutation/newline scans,
large replay staging, resize initialization and submit validation remain.
No hard 0.5 ms slice certification is claimed.

Release make all, the editor suite with active allocation guards and final
editor fuzz are green. Final full make check exits 2 at the unchanged refwin
preflight; its standalone retry is green. That final full-check acceptance
failure is recorded explicitly in the worker report. LeakSanitizer is disabled;
the coordinator must rerun the complete suite with leaks enabled.

The exact red/green, public decisions, acceptance gaps and final verification
are in [the mandatory worker report](../../docs/worker-reports/edit-zzj.13b-s9.md)
and [the decision record](../../docs/decisions/edit-zzj.13b.md). No new globals.
The earlier module integration records follow unchanged.

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

# edit-czn session 9 — deep-file line starts

The reported log_1g G1 prefix replay is reproduced and traced to the editor's
legacy post-edit view normalization, before T2. The editor now imports its
certified viewport/preceding anchors into the existing fixed view cache,
retains a capped backward-scan anchor through a boundary line join, and reuses
layout row starts. Unresolved clipped rows use capped tail scans or the
existing partial lineidx query; no lineidx implementation/policy change.

Focused regressions cover unbuilt metadata at beginning/deep/EOF positions,
character/Enter/backspace edits, and deletion of the preceding viewport
newline. Existing long-clipped-line first-frame/edit/undo coverage remains
green. Final-source ASan/UBSan check passed 59 binaries and replay CLI (M)[AC],
leaks disabled. Editor fuzz completed 61 seconds, 12821 executions clean
(M)[AC], against 60 seconds (G). The full existing benchmark matrix and final-source confirmations completed
in TRACK mode; zero typing allocations were observed (M)[AC]. Forced gcc
release make all exited zero after the host clock correction. Loaded-box
p99 misses and native EGL fallback prevent a G1 gate certification. Giant
line/query work outside the certified-anchor cap remains unproved; index
restart/repair and metadata maintenance belong to their separate beads.

Full red/green, exact trace attribution, final build/row verification, and
limitations: ../../docs/worker-reports/edit-czn-s9.md. Design:
../../docs/decisions/edit-czn.md.

## edit-zzj.16 session 9 — capacity and index repair

P1-1 sections 1 and 6 now have focused red/green editor regressions. Capacity
admission precedes ordinary replacement, insertion/deletion and undo/redo tree
mutation; adjacent short index chunks are repacked instead of retaining one
per-character tail. Refusal preserves the existing negative-error loop policy.
Dirty indexes use prepaid snapshot restart and bounded preparation/refresh,
including inactive buffers, staged initial results and retirement/admission
retry. Unreserved indexes retain bounded seek to the full content rather than
an estimated line target. Active replay's provisional tree is excluded.
large.c retains estimated viewport numbering and its exact publication policy.
No new globals, indentation algorithm or IPC-drain changes.

Design: ../../docs/decisions/edit-zzj.16.md. Complete red/green, final verification,
benchmark limitations and coordinator leak-on requirement:
../../docs/worker-reports/edit-zzj.16-s9.md.

Final verification (M)[AC]: gcc 13 strict make all exits zero; clang 18
ASan/UBSan make check passes 60 test binaries and replay CLI with leaks disabled.
Editor and lineidx fuzz each complete 61 seconds clean against 60 seconds (G).
The release editor suite and focused prepaid repair guard record zero allocations.
Paired TRACK benchmark timing/structural limits and the coordinator leak-on rerun
are fully recorded in docs/worker-reports/edit-zzj.16-s9.md.

# edit-g11: edit-time minimap flash

The stale-map tab exclamation mark is removed. Pending indexing keeps the
previous ready minimap until its replacement can be filled, with a desaturated
edge bar when no ready image is usable. Submit scheduling and glyph lookup
are unchanged. The submitted-frame null-backend regression checks amber
paint, retained cells, chrome markers, final revision and typing allocations.
Paint audit, red/green, builds, sandbox restrictions and comparison-drive
handoff are in [the worker report](../../docs/worker-reports/edit-g11.md).


# edit-d5o: pixel minimap composition

Compose now attaches the miniature as an independent frame pixel region.
Summary geometry and pointer navigation derive from the PRD line spacing,
and open-time arenas reserve all pixel/scratch/summary storage. Pending
indexing retains the ready image and viewport. Backend snapshots permit
continued UI composition without borrowing mutable pixels. Validation,
trace-derived compose cost and remaining display/drive verification are in
[the worker report](../../docs/worker-reports/edit-d5o.md).

Final outcome: strict GCC release build and the focused sanitizer/release
region checks pass; the release typing allocation guard is active and reports
no allocations. The full sanitizer driver stops at blocked IPC setup; every
suite was run separately, with native X/IPC restrictions and isolated reruns
itemized in the report. Display readback and the Astra drive remain pending.

## edit-a2g continuation

The former remaining probes now run by default: estimated deletion/history
replay stays bounded, lazy first layout repairs following logical rows, and
cold wrapped edits follow certified resident metadata. The edit-2ty near-end
imports remain intact; release allocation/latency checks pass. Fully deferred
explicit coordinates/byte jumps and nonresident wrap queries remain as described
in the worker report; native verification and the next Astra drive are pending.
Evidence: ../../docs/worker-reports/edit-a2g.md.

## edit-5ih: font adoption at the frame boundary

Startup reads EDIT_FONT_FAMILY / EDIT_FONT_FILE while the P6.4 loader is absent.
Family/file selection and DPI/zoom now use one lifecycle in editor_font_size:
current plus detached worker replacement, each with an async Unicode owner.
Preparation resolves and validates the primary, builds ASCII in prepaid storage,
and passes that same face to the runtime glyph cache. Mailbox delivery plus
physical completion permit adoption only after backend/frame/input retirement.
Detached glyph jobs must complete and drain before their version can be reused.
Grid/view/minimap geometry and the retained pixel viewport follow main's zoom
path. Resolved paths survive zoom; direct arena reads avoid stdio heap activity.
Selection failure restores embedded DejaVu and emits one worker diagnostic.

Strict builds, combined selection/Unicode/zoom tests, active allocation-guard
results and native display/launcher exceptions are recorded in
[the resolver report](../../docs/worker-reports/edit-5ih-resolve.md).
