# edit-2ts — startup must not await GPU discovery

The bead supplies Astra launch/first-text video evidence and a warm Sublime
reference. This worker cannot open the display or read evidence outside its
worktree, so it does not claim a native startup measurement or verified power
stamp for that supplied comparison.

The existing worker initializer is synchronous from the editor's perspective:
`editor_open` pumps its mailbox until both receipt and physical completion,
before file acquisition. Therefore any loader, driver or Present probe stall
is also a first-paint stall. The existing Present probe waits up to (E, code
bound) 1500000000 ns / 1000000000 ns per second = 1.5 s per attempted swap.
An absent completion terminates the swap loop at that attempt; these bounds
are possible costs, not attribution of the recorded video interval.
The ASCII atlas is checked-in data selected by pointer, not baked at launch.

The implementation initializes raster first (null for a
headless descriptor), and keep the requested GPU descriptor and state separate.
An independent discovery worker pool prevents GPU initialization from blocking
file-prefix work on the ordinary bulk worker or raster work on its raster lanes.
Only a completed GPU initializer can be considered for adoption, after the
bootstrap frame has been presented and retired. Raster continues serving
input throughout discovery. Failure logs the existing diagnostic once and
leaves raster selected, with no retries.

No startup deadline is added. The dependency bound is (P, program ordering)
zero UI waits for GPU discovery: the UI tests a completion flag and returns to
ordinary work while discovery is pending. This avoids choosing an unmeasured
wall-clock cutoff. Shutdown remains quiescent and may wait for an external
driver call to return. All descriptors, worker arguments and state stay owned
until physical completion.

The asynchronous production GL initializer must not swap test frames onto the
visible window. It rejects a server without DRI3 before loading libraries, prepares its draw
state without test swaps, and validates
matching PIXMAP serial/MSC/mode on the first real frame using the existing
completion machinery. The strict initializer retains its existing warm-swap
proof for explicit no-fallback diagnostics. No completion timeout is added or
changed. Raster storage is retained through close after successful adoption,
so backend switching cannot join a delayed raster worker on the UI thread.
Native acceptance and the next Astra comparison remain coordinator work.

Startup trace stamps are independent of input/frame markers and retained
through timing-ring wrap. The dump keeps the existing timing-record format;
startup stamps are emitted as additional timing records on the UI ring.
`tracedump` reports each stage relative to process entry, with arithmetic shown
in the output. Headless stages that have no native counterpart remain absent;
no synthetic display-connect time is reported.

The optional `render_backend.finish_init` callback is a worker-only finishing
phase. GL subscribes to Present there, after the first raster frame has retired.
It cannot subscribe during bootstrap initialization: a queued raster PIXMAP
completion could otherwise share the first EGL swap's serial. Later raster
frame IDs are strictly increasing, so they cannot reproduce that retired first
serial. Adoption again checks both physical worker completion and raster
retirement. Typing continues on raster during the finishing phase as well.
The strict initializer keeps its warm-swap proof; background GL uses the existing
terminal device-completion guard on its first actual swap. Native verification
of that handoff remains required.

The discovery mailbox remains drained after final selection. `work_publish`
exposes a payload before writing its eventfd notification; a late notification
must not leave the shared epoll set permanently readable at idle. A deterministic
regression injects that late notification after selection. No polling timer or
retry is introduced.

`file_prefix_read` stamps UI observation of the worker's PREFIX_READY message;
it is not a separate timestamp of the underlying read syscall. `process_start`
is the earliest application timestamp at main entry, excluding pre-main loader
and process-creation work. `first_present` means backend acceptance, not measured
scanout. The null test's origin precedes its fixture preparation and headless
open, so its elapsed times must not be compared with the video or Sublime.

Legacy native frame-zero markers remain in their existing gate aggregates.
This bead adds a distinct startup section without redefining typing gate
arithmetic. Editor benchmark rows wait for discovery to finish before naming
the selected backend or enforcing `--require-gl`, while the application paints
throughout discovery.
