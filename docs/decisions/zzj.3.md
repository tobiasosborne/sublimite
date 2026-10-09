# edit-zzj.3 — P3.3 M0 editor loop

## Backend decision

Consultant decision at 12:45 on 2026-10-09: use the landed P2.5 CPU raster
backend through the frozen `src/render/render.h`. No GL code is included.
`src/main.c` fills a `render_backend` at one factory call site and passes that
handle to `editor_open`. The editor has no raster or GL dependency; the eventual
GL factory can replace the application factory without changing the loop.
Tests and fuzz use the same loop with `render_null_backend`; live tests and the
G1 bench use `render_cpu_backend` on Xvfb `:99`.

## Ownership and loop

One UI thread owns the editor, piece tree, undo log, view, layout, input queue,
backend adapter and journal. The editor owns a `work_pool`, optional X11
platform, file mapping and line index. Backend initialisation runs on a setup
worker and joins before input is accepted. Close stops submissions, flushes the
journal with the shared mailbox router, shuts down the backend and index, joins
workers, then releases the tree/arena. Public event injection uses a fixed queue
and the same mutation/layout/submit path as native input.

The loop first routes completions, then drains input. It checks pending input
between layout or view continuations. Layout uses 32 clusters per call (E,
initial conservative slice size); a turn yields after 0.5 ms (G) of cooperative
work. Another turn drains XCB before continuing. Within a turn, queue inspections
allocate nothing. Backend submission and its validation are indivisible API
calls. Diagnostics retain the longest individual slice.

The adapter enforces queue depth one (G), through both T5 and T6. While a frame
is active, input still mutates the UI-owned tree and coalesces damage for the
next frame. No second backend frame is queued. The backend copies grid metadata
on submit, so later layout may reuse the caller grid. Resize waits for the
active frame and pending old-size frame, then applies the latest resize within
the startup reservation. Expose, viewport changes and interrupted layout restart
a full frame. Ordinary edits use `layout_edit` strips, with any additional
cursor/selection rows scheduled before submit.

The platform pump uses `plat_run_for` and its callbacks. A callback requests a
local pump return using `plat_quit`; application quit is a separate editor flag.
This preserves XKB compose/repeat, queued event handling and the existing
platform poll implementation without adding another X protocol dispatcher.
The platform's periodic blink timer is disabled. The editor supplies the next
blink/journal deadline as the poll timeout. Workers cross into UI only through
the existing `work` mailboxes.

## View, undo and journal integration

The temporary M0 table is one small function in `input.c`: printable bytes,
Enter, Backspace, Delete, arrows, Ctrl+Z and Ctrl+Shift+Z. Shift selection and
Ctrl word arrows use the existing view movement. Ctrl+S is omitted until the
save/journal transaction API lands. P4.3 can replace the table function.

View movement plans delete ranges without mutating the tree. Actual insertion
and deletion go through undo. An empty `VIEW_TYPE` invokes only view's boundary
repair/follow; its zero-length piece insert is a no-op. One explicit undo group
per mutating key is the M0 granularity (E); automatic typing-burst grouping is
not used. Cursor and anchor fit the undo contract's opaque state blob.

A bounded ring stores only each group's replacement offset and old/new lengths.
It follows redo invalidation and undo's whole-group trimming. It does not
duplicate undo byte storage. On replay, restored bytes are read from the current
tree and staged as an equivalent replacement for the journal. This also captures
a successful replay prefix before reporting an error. Subsequent turns stop on
mutation errors; partial replay retry is not exposed. Positive module errors are
converted to negative loop errors, preserving their cause in editor stats, so
NOMEM cannot collide with the public MORE/CLOSED statuses.

Undo restores cursor/anchor but does not restore viewport offsets. A regression
found that shortening a scrolled document could leave `first_byte` past EOF.
Layout RESET now defers full layout until view repair/follow finishes. Replay
follows the restored cursor, preserving its selection anchor, before layout.

Journal operations and insertion bytes are staged in startup-reserved storage.
They append after successful backend submit, in mutation order. The submit hook
runs before journaling, presentation and completion handling, defining the
malloc assertion endpoint. A full staging queue pauses input consumption until
the pending frame can submit. Journal pumping and blocking exit flush stay
outside input-to-submit. Sticky journal errors are observable and stop later
turns; accepted records are never deliberately discarded.

Reservations are finite: input queue 1024 events (E), journal staging 512 KiB
(E), replacement history 32768 keys by default (E), arena 64 MiB plus copied
original size (E). Exhaustion returns an error. A replay insertion exceeding
staging capacity is refused before replay. This is M0, not an unlimited paste
or session-checkpoint implementation. `edit <file>` keeps a unique adjacent
session journal and prints its path at exit. It neither overwrites a previous
journal nor saves the source automatically. Recovery UI is later work.

File setup uses `file_open_begin`/`file_attach`. Exact lazy piece newline counts
are warmed during open, before input acceptance; an independent snapshot index
build then runs on the bulk pool. The benchmark positions by line only after
index publication, otherwise by byte offset. Edits invalidate/cancel affected
index work, while layout maintains its exact line total from edit deltas.
Automatic index rebuilding after edits is not part of this M0 loop.

## Trace and measurement endpoints

- T0 retains the physical event timestamp when supplied, otherwise drain time.
- T1 stamps a recognised key as it is drained, before mutation/frame work.
- T2 follows successful mutation and cursor repair; replay traces its successful
  prefix too.
- T3 follows completed layout, immediately before backend submission.
- T4 is recorded by the frozen adapter inside successful `present`, before
  hooks. T5/T6 retain the backend's matching frame ID through mailbox routing.

The public submit callback reports the return time of `render_backend_submit`.
The present callback separately reports the return time of `present` (the T4
endpoint). The G1 bench retains every sample through callbacks, independent of
trace-ring wrap. It reports both endpoints, with the 1.0/2.0 ms (G) limits.
Default invocation exits nonzero on a raster G1 or G11 miss. `--track` retains
real limits/pass fields but suppresses timing verdicts for the shared box;
`bench/editor_bench.args` selects that mode. Structural/IO failures still fail.

G1 injects 10000 keys (P, acceptance sample count), alternating printable insert
and Backspace at a stable mid-file column. Every key mutates the tree, reaches a
matching submitted/presented frame, and is journaled. This measures dirty-strip
typing without conflating repeated horizontal scrolling with G3. It waits for
the preceding frame's T5/T6 outside each key's measurement. It does not establish
latency under an externally imposed saturated arrival rate.

Law 2's counting window starts at the ingress hook and ends at submit return.
Both backends use the real release allocator guard. XCB event/reply allocations
in present/completion handling remain outside that window, per the P2.0 addendum.
The unit allocation workload drains bounded bursts and checks 10000 keys (P)
per backend; the bench additionally checks individual raster frames.

An additional release run at load1 12.79 (M)[AC] exposed a counting-window
mistake: a burst can span several turns, and the process-wide guard then counted
XCB packets in a later platform pump. The optional `on_io` instrumentation hook
now brackets platform/present/completion work. The driver sums guard counts over
every mutation/layout/submit segment, suspending only in that external IO
boundary and resuming afterwards. No text mutation, layout or submit occurs
inside it. This applies the existing P2.0 library exemption without filtering
any allocations from our typing work or requiring a quiet box.

## Idle policy and remaining gate

Blink flips every 500 ms (E, chosen to meet the wakeup limit), then stops after
10 seconds (G) without input. Unfocus disables the deadline immediately. A
durable journal has no timer. The final stop leaves the caret steadily visible
while focused. With no work/input/timer, the real application blocks indefinitely.

G11 uses the process CPU clock as an upper bound on summed app-thread CPU time,
including raster workers, and counts UI `poll` returns. Idle observations remove
only their known external test-deadline timeout. The null pool is parked, so
its UI poll count covers the app's idle activity. The raster backend also polls
on its fence worker: its UI wake count alone already exceeds the gate.

The initial idle observer incorrectly made an additional bounded settle poll
after an already-completed null frame. It was corrected and only idle rows were
remeasured. Corrected results (M)[AC], shared box, TRACK:

| Backend | Process CPU p50/p99 | UI polls in blink window | Load1 | Result |
|---|---:|---:|---:|---|
| null | 0.073/0.130 ms | 20 | 3.40 | within 0.1/0.2 ms (G), 2/s policy |
| raster | 0.650/1.017 ms | 100 | 3.57 | G11 miss |

Both rows observed zero background UI returns after timeout and while unfocused
(M)[AC], at their listed starting loads. Raster needs a backend caret/damage or
completion-cadence improvement to meet G11. The frozen full-width strip backend
currently starts raster jobs and asynchronously wakes UI for their completion
and presentation. No backend or frozen header was changed to hide that cost.

The cooperative budget also cannot preempt the existing view/undo/piece calls.
Pathological graphemes, very large selections, or synchronous replay can exceed
the slice target; main has no sliced replay API yet. M0's measured longest
individual G1 slices were 0.053 ms null and 0.196 ms raster (M)[AC], load1 3.39.
These measurements do not prove a hard bound on arbitrary commands.

Out-of-scope view finding: End on the empty last line of `"a\n"` (cursor 2)
returns cursor 1 because `line_end` strips the previous line's LF at EOF.
Home/End were removed from the temporary table, keeping the specified M0
bindings; view source was not changed.

## Verification outcome

The module STATUS retains RED/GREEN lines, all final make result lines, stamps
and fuzz evidence. Final release tests, including the IO-aware real guard,
passed at load1 5.71 (M)[AC], BAT0=Full. Full ASan/UBSan `make check` passed all
26 test binaries and the replay CLI checks (M)[AC], same starting load. Leak
checking was disabled for the sandbox and remains a coordinator rerun.
`make all` and `make fuzz` returned exit 0; 15 fuzzers were built (M)[AC].

After the viewport repair, a 301-second run completed 12554 inputs with no
findings (M)[AC], load1 6.16. The final IO-boundary build completed 4270 inputs
in 61 seconds with no findings (M)[AC], load1 5.71. The actual `edit` binary
was also driven on `:99` with native typing and WM close; its flushed journal
replayed to the independently expected `ab\nx`. Raster G11 remains the unmet
acceptance criterion; these results are not a claim that the bead is gate-complete.

## First human test drive on the real display (2026-10-09 14:12–14:15, session 6)
Approved by Tobias via the consultant (14:20 go). Binary: main.c at 51de929 rebuilt in the coordinator's scratchpad with two env-gated changes (honour `EDIT_ALLOW_REAL_DISPLAY`; `EDIT_TRACE_DUMP=<path>` calls `trace_dump()` after `editor_close()`), linked against the same `build/libedit.a`. Both changes are part of edit-457.18. Command: `EDIT_ALLOW_REAL_DISPLAY=1 DISPLAY=:0 EDIT_TRACE_DUMP=… edit /tmp/edit-corpus/ascii_code.c`, raster backend, one window, mapped 14:12:22, closed by a WM close request at 14:15:22, exit 0, session journal kept.
Tobias typed, moved around and deleted for about 30 s: "all good, I didn't notice any weirdness". No mis-render, stuck key or crash.
Trace ring (tools/tracedump + per-stage variant), power "Not charging" [AC], 1-min load 7.6–8.2 (about 12 Codex workers running), **TRACK only**:

| Span | n | p50 | p99 |
|---|---|---|---|
| T1 − T0 (dequeue) | 886 | 0.778 ms | 3.653 ms |
| T2 − T0 (mutation done) | 541 | 1.121 ms | 3.958 ms |
| T3 − T0 (render done) | 886 | 0.839 ms | 11.266 ms |
| **G1 T4 − T0 (present submitted)** | 886 | **1.544 ms** | **14.914 ms** |
| G3 T5 − T0 (device done) | 886 | 2.154 ms | 17.542 ms |
| T6 − T0 (present complete) | 886 | 9.677 ms | 27.514 ms |

n = 886 input events (keys incl. releases, pointer), 541 of them mutations. Caveats: T0 is the X server timestamp (1 ms granularity), so every T0 span carries up to +1 ms quantisation; the T1 − T0 p50 of 0.78 ms is mostly that. On a box at load 8 these say nothing about the G1 gate (1.0/2.0 ms); the quiet verdict comes with the M0 bench (edit-zzj.5) and an evdev T0 (edit-e6x.17 needs group `input`).
