# edit-2ts — startup must not await GPU discovery

The observed launch-to-text interval supplied in the bead is (M)[bat]
3.117 s − 0.830 s = 2.287 s, rounded there to 2.3 s. The supplied warm
Sublime reference is (M)[bat] 136 ms, taken from the Astra startup metrics.
This worker cannot open the display or read evidence outside its worktree;
these are coordinator-provided observations, not new worker measurements.

The existing worker initializer is synchronous from the editor's perspective:
`editor_open` pumps its mailbox until both receipt and physical completion,
before file acquisition. Therefore any loader, driver or Present probe stall
is also a first-paint stall. The existing Present probe waits up to (E, code
bound) 1500000000 ns / 1000000000 ns per second = 1.5 s per attempted swap.
An absent completion terminates the swap loop at that attempt; these bounds
are possible costs, not attribution of the recorded video interval.
The ASCII atlas is checked-in data selected by pointer, not baked at launch.

The implementation under test will initialize raster first (null for a
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
visible window. It will subscribe to its private Present events without test
swaps, reject a server without DRI3 before loading the libraries, and validate
matching PIXMAP serial/MSC/mode on the first real frame using the existing
completion machinery. The strict initializer retains its existing warm-swap
proof for explicit no-fallback diagnostics. No completion timeout is added or
changed. Raster storage is retained through close after successful adoption,
so backend switching cannot join a delayed raster worker on the UI thread.
Native acceptance and the next Astra comparison remain coordinator work.

Startup trace stamps will be independent of input/frame markers and retained
through timing-ring wrap. The dump keeps the existing timing-record format;
startup stamps are emitted as additional timing records on the UI ring.
`tracedump` reports each stage relative to process entry, with arithmetic shown
in the output. Headless stages that have no native counterpart remain absent;
no synthetic display-connect time is reported.
