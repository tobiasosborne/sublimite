# edit-og2: private test IPC and display-specific CLI primaries

The CLI window test must control its primary endpoint before forking the editor.
`tests/ipc_isolation.h` creates a private mode-0700 runtime directory and sets
both `XDG_RUNTIME_DIR` and a unique `EDIT_IPC_NAMESPACE`. Filesystem discovery
therefore stays private; tests which unset the runtime variable also inherit a
private abstract name. The helper registers process-owned exit cleanup without
introducing globals. Forked clients inherit the endpoint and cannot remove the
parent's directory when they exit. Cleanup does not follow fixture symlinks.
The existing IPC tests and the IPC-launching editor/IPC benchmarks use this
helper. The shell launcher creates its own private runtime under its existing
trapped temporary directory.

CLI primaries are selected by the effective display, after the existing safe
display selection and before IPC arbitration. New display-specific IPC entry
points encode the display string losslessly as a hexadecimal endpoint suffix.
The suffix applies to filesystem sockets, their lifecycle locks, abstract
sockets, and the private filesystem fallback. Different display strings cannot
select the same primary. Same-display startup still arbitrates and hands off
normally; `--new-instance` retains its existing behavior. General IPC entry
points retain their original endpoint names and wire protocol.

This keeps the change in endpoint discovery and the CLI startup path. It needs
no editor-loop changes, protocol negotiation, persistent display metadata,
process-environment inspection, new global state, or typing-path allocation.
The IPC-launching editor benchmark uses the display-specific server entry point
so its real CLI children find the intended primary.

Display comparison is deliberately exact: aliases such as `:99` and `:99.0`
may create separate primaries, which conservatively avoids cross-display
handoff. Empty display strings return `IPC_INVALID`; encoded names which do not
fit the existing bounded endpoint storage return `IPC_LIMIT` before binding or
sending. The launcher reports the startup error rather than silently routing
an oversized name elsewhere. Existing running binaries with unsuffixed CLI
endpoints are not discovered by the new CLI.

The CLI regression first creates a same-display decoy in the inherited runtime,
then isolates the launching child. A separate regression deliberately shares
the runtime with an IPC-only decoy declaring a different display. Both assert
that the child creates its own window and the decoy receives no handoff. The
suite also checks that a same-display launcher still hands off successfully.
The different-display decoy never opens a window; all actual windows remain on
Xvfb `:99`.

No timing gate or benchmark rerun is justified: the typing path and module hot
paths are unchanged. Correctness and repeated concurrent launches are the
acceptance evidence in `docs/worker-reports/edit-og2-s9.md`.
