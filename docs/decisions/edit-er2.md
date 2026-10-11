# edit-er2 — shared inotify service (src:astra)

The file module owns `file_watches`, a sanctioned process-wide service alongside
`file_bus`. Per-file inotify instances exhaust the per-user instance allowance
before the editor's tab reservation. The red census and regression evidence are
in [the worker report](../worker-reports/edit-er2.md).

`pthread_once` installs the nonblocking, close-on-exec descriptor. Installation
failure retains its errno. The descriptor belongs to the service until process
exit; `file_close` unregisters a member and releases its watch rather than
closing the shared descriptor. No new public init/fini call is necessary.

The fixed registry has **8192 entries (P)**: the existing maximum live reservation
**4096 (G)** plus maximum retained reservation **4096 (G)**, both enforced by
`editor_open` in `src/editor/open.c`; arithmetic is live + retained. Exhaustion
returns ENOSPC. The registry is process-wide, so these entries are shared across
any editors in that process. All watcher setup, dispatch and close calls belong
to the single UI owner, matching the existing UI ownership of file check state.
Dispatch takes no locks and allocates no storage; worker results still cross
through the existing work mailbox.

Keep the parent-directory watch and per-file basename filter. Linux may assign
the same directory watch descriptor to several file members. A refresh first
acquires the current parent watch and releases the old descriptor only if it
changed and no peer uses it. Closing a file removes a kernel watch only after
its last member leaves. IN_IGNORED invalidates every member of that descriptor;
parent move/deletion requests refresh, and queue overflow requests every member's
identity check. Each file retains its own descriptor and basename association.

Polling retains the existing bounded read buffer and syscall bound. Events are
demultiplexed to every matching member and affected checks are submitted during
that poll, including checks for peers of the caller. Requests remain pending
when their prior result has not been decoded or submission is saturated.
CHECK_DONE retains its existing generation validation and installation path.
The prompt literal in `src/editor/paint.c` remains byte-identical:
`Source changed: R reload / K keep`.

The borrowed shared descriptor forces a caller adaptation in
`src/editor/buffers.c`: tolerate epoll EEXIST, preserve registration while peers
use it, and release it on the last member's retirement, reload or failed
preparation. This is the only editor implementation change. The regression
exercises retained-tab eviction followed by an external change in a surviving
tab. No IPC or main implementation change is required.

The acceptance fixture uses **200 files (G)**, directly from the bead, with an
editor tab reservation equal to that fixture. Main's existing default live-tab
reservation is **128 (G)**, derived from `editor_open`'s default; increasing that
default is outside this file-service bead. The new CLI handoff regression uses
the configured editor as the primary and the real CLI entry point as each client.
A companion local request regression covers the same preparation and watcher
registration path when sandbox endpoint binding is denied.
