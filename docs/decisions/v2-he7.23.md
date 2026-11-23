# W0.9 · The four allocations in v1's R1 window

At 0.48 the inspected v1 build reported `oracle law2: active=1 allocations=4 scopes=2` for R1 (wrapped Return).
This bead attributes those four calls. **Outcome: (a), a real allocation on v1's typing path.** They are not
an artefact of the adapter's window. All four are libxcb packet buffers. libxcb allocates them on the UI thread
inside the backend submit, from `xcb_flush` in `cpu_paint_margins`. The adapter window is unchanged.
`tests/contract/v1-inspector.patch` (SHA-256 `54bcf05f…fda1a`) and `navigation.py` are unchanged. R1 stays
"recorded, not asserted" on v1, as the Law-2 count rule in `wave0-contract-design.md` requires for a nonzero
case.

## Method

The adapter's measurement is unchanged. `on_ingress` opens a calling-thread scope at admitted input.
`on_submit` closes it on a successful frame that carries `last_sequence`, and `close_result` ends any
unfinished scope. The `on_io` brackets exempt platform, present and completion I/O (`docs/decisions/v2-he7.11.md`).

The temporary instrumentation in `/home/tobias/Projects/sublimite-v1-inspect` had two parts:

- `src/base/malloc_guard.c`: each call counted in a thread scope also recorded its glibc `backtrace()`, the
  call kind and size, and the ordinal of its scope. `backtrace()` was warmed up once under a recursion
  guard, so libgcc's lazy load is not counted. The recorder did not count its own allocations.
- `src/editor/inspect.c`: `editor_inspect_close` printed the records with `dladdr` module offsets, after
  the last scope ended and before the existing `oracle law2:` line.

The binary was built into a separate output directory, `build/w09/diag`, so the stamped oracle in
`build/contract-oracle` was not touched. That directory got a hand-written run manifest naming the
diagnostic executable's digest. Executable frames were resolved with `addr2line -f -i`. libxcb has no debug
symbols on this host (`libxcb1 1.15-1ubuntu2`), so its frames were matched by disassembly. Afterwards both
files were restored from copies taken before the edit. `build_oracle.source_state` then confirmed that the
tree again equals the pin plus the exact patch.

```sh
oracle=/home/tobias/Projects/sublimite-v1-inspect
# (instrumented malloc_guard.c + inspect.c; originals saved under $oracle/build/w09/orig/)
cd "$oracle" && CPATH=$oracle/build/sysroot/usr/include make -j8 B=build/w09/diag build/w09/diag/sublimite
cd <v2 checkout> && make xdriver all
python3 -B tests/contract/cases/navigation.py --build-dir "$oracle/build/w09/diag" --display :172 --case R1
addr2line -f -i -e "$oracle/build/w09/diag/sublimite" 0xce2cb 0xd4a37 0x3de61 0x3eef9 0x2c6ff 0x2af15
objdump -d --start-address=0xc1c0 --stop-address=0xc260 /lib/x86_64-linux-gnu/libxcb.so.1
objdump -d --start-address=0xc4a0 --stop-address=0xc4c0 /lib/x86_64-linux-gnu/libxcb.so.1
```

The run was a single first attempt with power `Charging` [AC] and start load 6.54 / 4.11 / 5.47 (M, 1, 5 and
15 min). All four R1 assertion groups passed and the editor exited 0. The editor log reproduced the 0.48 count
exactly: `oracle law2: active=1 allocations=4 scopes=2` (M). The receipts are in
`build/contract-navigation-x1l0lg21/R1/editor.log`.

## The four calls

| # | Scope (of 2) | Call | Size (M) | Packet (P, from libxcb's `read_packet`) |
|---|---|---|---:|---|
| 0 | 1 | `malloc` | 36 | 32-byte non-reply packet + 4 bytes for `full_sequence` |
| 1 | 1 | `malloc` | 16 | `struct event_list` queue node (two pointers) for that packet |
| 2 | 1 | `malloc` | 36 | a second 32-byte non-reply packet |
| 3 | 1 | `malloc` | 16 | its queue node |

All four share one stack. Below, v1 lines are given at pin `6fec2e8`. `raster.c` and `render.c` are not
touched by the patch. The `editor.c` lines are mapped from the patched build by subtracting the patch's
inserted lines.

```
libxcb.so.1 +0xc258 / +0xc4be   read_packet: malloc(bufsize) / malloc(sizeof(struct event_list))
libxcb.so.1 +0xca96             _xcb_in_read            (hidden symbol; libxcb 1.15 call order)
libxcb.so.1 +0xcc4f             _xcb_conn_wait          (polls POLLIN|POLLOUT and reads whatever is pending)
libxcb.so.1 +0xcda7             _xcb_out_send / _xcb_out_flush_to
libxcb.so.1 +0xddd8             xcb_flush
cpu_paint_margins      src/raster/raster.c:411    xcb_flush(st->conn)
cpu_submit             src/raster/raster.c:870    (inlined into the frame above)
render_backend_submit  src/render/render.c:421    b->ops.submit
submit                 src/editor/editor.c:666    render_backend_submit(e->backend, &e->grid, …)
editor_step            src/editor/editor.c:1385   rc = submit(e)
editor_run → editor_main → main
```

The libxcb frames are identified as follows. At `+0xc253` the allocation size is `0x20`, plus 4 when the
response type is not 1 (`XCB_REPLY`), and is bounded by `0x7ffffffe`. That matches 1.15 `read_packet`'s
`bufsize = length + eventlength + nfd·sizeof(int) + (response_type == XCB_REPLY ? 0 : sizeof(uint32_t))`
with its `< INT32_MAX` check. At `+0xc4b9`, `malloc(0x10)` follows the store of `c->in.request_read` into
`buf + 32`, the `full_sequence` field. The three internal frames are hidden symbols. Their names come from
libxcb's call order, not from symbols (E).

The packets are on the raster backend's own connection. `cpu_init` connects it separately
(`st->conn = xcb_connect(NULL, …)`, raster.c:519), so they are not keyboard or pointer input. On that
connection the GC is created with `XCB_GC_GRAPHICS_EXPOSURES = 0` (raster.c:566–568). Present input is
selected for `COMPLETE_NOTIFY | IDLE_NOTIFY` only (raster.c:558–559). Of the packets that can arrive there:

- the sync `QueryFence` reply is a reply, so it gets no +4 and its buffer is 32 bytes;
- a `PresentCompleteNotify` is 40 wire bytes (`length = 2`), so its buffer is 44 bytes.

A 36-byte buffer therefore means a 32-byte XGE event with `length = 0`, which is `PresentIdleNotify`, or an
X error. The fence worker and `raster_poll_completions` treat an error as device failure (raster.c:319,
1127). The run had no failure, so the four calls are most likely two `PresentIdleNotify` packets with their
special-event queue nodes (E: the event type was not captured). Which Present notification it was does not
change the classification.

**Which input.** Scope 1 holds all four. Among R1's inputs, `on_ingress` fires for the Go To Line keys
(Ctrl+G, `6`, `5`, Return: `input.c`'s `go` path) and for the wrapped Return. Ctrl+S goes to
`editor_save_request` without ingress. The click is pointer placement. Only two scopes opened (M,
`scopes=2`). The wrapped Return is the last admitted input. The Go To Line keys are admitted earlier, and
the runner waits for a settled snapshot before the click. So scope 1 is the Go To Line burst (P, ordering),
and the wrapped Return's own scope measured **0** (M). The allocations come from the case's setup, which the
runner sends as four consecutive keys without waiting between them. The wrapped Return line itself did not
allocate.

## Classification: (a)

The allocation happens on the UI thread. `submit` reaches it through `render_backend_submit → cpu_submit →
cpu_paint_margins → xcb_flush`. That is the last step of input → mutation → layout → submit, on a frame
produced by admitted keystrokes. Moving the window's ends cannot remove these calls without also removing
the backend submit, which Law 2 includes. `editor.h`'s `on_io` contract says so as well: "No key
mutation/layout run/backend submit occurs inside it". It is not an adapter artefact.

- **Same count under v1's own guard.** The process-wide counter that v1's G1 tests use would count these
  four calls too, because they are on the counting thread itself.
- **Not covered by v1's exemption.** The bytes are post-submit completion packets, which v1's P2.0 addendum
  (item 3) exempts. v1 exempts them only on the fence worker (`fence_poll_*`, raster.c:248–273) and in the
  `on_io` brackets around `present()`. In libxcb, whichever thread enters connection I/O while packets are
  pending reads them and allocates. When the UI thread's margin flush gets there first, the allocation
  lands on the typing path.
- **Timing.** This needs the previous frame's Present packets to be unread when the next submit flushes.
  That happens under back-to-back input such as R1's unsynchronised Go To Line keys, which is the
  fast-typing case Law 2 is for. The wrapped Return was sent only after a settled snapshot, and its scope
  measured 0. v1's unit tests (for example `raster_test.c:946`, `cpu_submit` with allocations 0) run with no
  completion traffic pending.

This is **v1 finding 8**. Lines for the Wave 2 bead:

- Not carried into v2: `cpu_paint_margins` (src/raster/raster.c:411) calls `xcb_flush` on the UI thread during
  backend submit. libxcb's `_xcb_conn_wait` then reads pending Present packets on the backend connection and
  `read_packet` mallocs a buffer of 32 + 4 bytes per packet.
- Not carried into v2: the same `read_packet` path mallocs a 16-byte `struct event_list` node per packet when
  it queues the packet (special-event or generic queue), on the same UI-thread flush.

The v1 call site is in the raster backend (L3 `gfx_plat`), not in layout. Every UI-thread libxcb write that can
flush on that connection is a candidate. Only raster.c:411 was observed (M). Unobserved, by the same mechanism:
the strip, region and cursor uploads at raster.c:236, 246 and 694 when they run inside submit (E). It is the
coordinator's choice whether this goes to the Wave 2 layout bead as briefed or to the bead that owns the
v2 X11 present path. For v2 the invariant is that no UI-thread call between ingress and submit enters
libxcb connection I/O on a connection that carries server-originated packets. The simplest way to meet it is
to have the completion owner drain or own that connection, so the UI thread only writes, and to verify it
with a submit-under-pending-completions test. That design belongs to the owning bead and is not decided
here.

## Follow-ups for the coordinator (not done here)

- The adapter reports one total per process. A per-scope report would let R1 assert 0 on v1 for the wrapped
  Return scope while recording the setup scope. That would be a patch change, which this brief permits only
  in case (b).
- `wave0-contract-design.md`'s count-rule note ("attribution in bead W0.9") can point here.

`make all` and `make check` passed in this checkout after the doc was added.
