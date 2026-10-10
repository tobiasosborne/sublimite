# edit-k60 — IPC wait integration

The editor now drops disconnected wait associations in its existing IPC drain
path. Main now gives primary and isolated `--wait` launches an adopted token
instead of opening the initial request with token zero. Existing IPC primitives
and the all-member tab-close accounting remain the implementation contract.
See [the worker report](../worker-reports/edit-k60-s9.md) for red/green evidence
and final verification.

## Drain ownership and bounds

Compare `server.wait_drops` immediately before and after `ipc_server_drain`.
On a change, query each occupied fixed association with
`ipc_server_token_live`, clear dead association records, then clear matching
`editor_buffer.wait_token` fields in the reserved buffer table. Keep their
buffers and tabs open, and preserve every live token and remaining-member count.
Clearing both sides prevents retained/reopened buffers from carrying obsolete
ownership. This also runs when the drain returns an error after dropping peers.

The sweep uses an automatic `IPC_MAX_CLIENTS` token array. Its work is bounded
by `IPC_MAX_CLIENTS` live-token queries and at most
`buffer_capacity * IPC_MAX_CLIENTS` token comparisons per drain. These are
configured reservation bounds (G), not latency measurements. It neither
allocates nor performs blocking I/O. The existing empty-input/finished-key-frame
barrier still surrounds IPC setup; no typing, indexing or capacity path changes.

## Launcher and endpoint ownership

Resolve the primary/secondary role using the existing startup policy. Secondary
launches retain the existing client handoff. An isolated launch only initializes
an endpoint-free server when it needs `--wait`.

Before creating the session file, trace state, editor, renderer or worker pool,
create the launcher pair and fork. Directory/path preparation is already done.
The child closes the launcher end, adopts the UI end, and passes its nonzero
token to `editor_open_request`. The parent closes the UI end, frees parsed args
and its server reservation, waits for the closed reply, and propagates failure
if the UI disappears first. Closing all request members can release the parent
while the UI continues running.

At this fork no peers have been accepted. In the parent, clear `owns_path`,
close the inherited lifecycle-lock descriptor and set it invalid before
`ipc_server_fini`. Thus finalization only releases that process's empty server
copy; it cannot unlink the child's endpoint or acquire a lock on their shared
open-file description. The child alone keeps endpoint cleanup ownership. Tests
probe the endpoint while the launcher blocks and also terminate the UI before
closure, detecting both premature unlink/lock retention and retained wait ends.

## Integration boundaries

No IPC header/socket implementation changes are necessary. The main additions
start at session-path construction, after the display-selection region changed
by `wt/edit-og2`. Its display-specific routing remains that worker's scope.
Only `src/editor/buffers.c`'s IPC drain region changes in the editor module.
No globals are added. No allocator, capacity/index-repair or indentation work
is included.

Main's production entry point is compiled directly into `tests/ipc_test.c`,
following the existing CLI-test pattern. Only backend selection and the UI loop
are replaced by a null renderer and pipe commands; argument parsing, stdin,
forks, server ownership, adoption, editor setup, tab closure and shutdown are
real. The test process temporarily becomes a child subreaper to reap the UI
after the launcher exits. Tests cover primary and isolated wait, all-member
closure including binary stdin, UI death, and unchanged nonwait execution.
The primary ownership probe uses the socket leaf reported by the UI, checks
its socket inode, independently acquires/releases its lifecycle lock, and
connects to it. It therefore remains valid when `edit-og2` changes main to
display-specific endpoint names. Test helpers/imports are placed away from
that worker's additions; three-way text merges of main and both changed test
files are clean.

No benchmark rerun is warranted: these are startup and off-input drain changes.
Allocation guards cover the new sweep and the existing mixed typing workload;
no timing gate verdict is claimed on the loaded AC box.
