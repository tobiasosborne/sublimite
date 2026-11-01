# edit-177r.21 — measurement boundary and proposed WSL policy

Phase 1 changes only the probe, its wrappers/build target, and documentation.
The measured capability matrix and the proposed degraded mode are in
[`wsl-share-capabilities-2026-11-01.md`](../reviews/wsl-share-capabilities-2026-11-01.md).
No production behavior or frozen `os.h` contract changes here.

The matrix's **safe as is / needs degraded mode / refuse** labels are proposed
product policy based on this host, not a claim of universal WSL behavior.
Successful mapping, flushing, stream-like filename creation, or closed-target
rename does not by itself authorize share saves. Preserving Linux mode and
ownership, source conflict checks and atomic replacement are separate obligations.
An ordinary production save currently fails without changing the original;
relaxing that failure requires a metadata-preserving transaction, not merely
a fallback rename. The coordinator must select that later write architecture.

Probe-policy estimates (E), with their derivations:

- Repetitions: an initial trial plus two repeats gives three trials. This detects
  disagreements; it does not estimate a tail probability.
- Observation window: two periods of the existing file-watch parent fallback,
  `2 × 1000 ms = 2000 ms`. This bounds an experiment, not acceptable latency.
- Helper bound: two observation windows plus a parent period,
  `2 × 2000 ms + 1000 ms = 5000 ms`.
- Child bound: three helper allowances for setup, mutation and teardown,
  `3 × 5000 ms = 15000 ms`. The supervisor terminates only its own timed-out child.
- Helper polling: five existing test-wait quanta (`5 × 1 ms = 5 ms`); notification
  timing explicitly includes helper scheduling. No cross-OS clock subtraction.
- Mapping extent: three native allocation-granularity units (E: a warm head,
  interior, and initially untouched tail). On this host that is measured as
  `3 × 65536 bytes = 196608 bytes` (M, native granularity and emitted length).
- Executable metadata fixture: octal `0751` (E), chosen to distinguish all three
  Linux permission classes and executable bits. The before/after values and
  owner IDs are actual Linux `lstat` results (M), not synthesized Windows modes.

Every destructive mapping experiment is a separate supervised child. The last
vectored handler logs otherwise unhandled faults; the normal production handler
gets its recovery opportunity first. Separate synthetic IN_PAGE_ERROR controls
verify this containment without claiming a real provider failure. The profile
lookup shim is limited to making the native Known Folder unavailable, so the
unchanged OS implementation uses its real environment fallback to a private
profile. It intercepts no filesystem capability being measured.

Stopping/restarting a distro, changing registry/profile configuration, installing
software, or using the live desktop is outside this experiment. Availability and
backing loss require a later Windows-driven disposable environment.

## edit-177r.28 — Windows WSL read-copy and explicit save refusal

Classify only the exact UNC servers `wsl.localhost` and `wsl$`, ignoring ASCII
case in the server and namespace prefix. Ordinary UNC, extended `\\?\UNC\`,
device `\\.\UNC\` (also accepted by the existing OS converter), and slash
spellings share the same policy. Server suffix lookalikes and other SMB shares
retain their current behavior. `os_path_is_wsl` is a Windows-only exported
CPU predicate; the only os.h addition is its declaration. No existing OS
operation, Linux implementation or identity representation changes.

WSL originals use the existing single-allocation read-copy acquisition, with
all descriptor/path pre/post validation retained. An injected copy_threshold
continues to select COPY versus MMAP for other files; WSL ignores that selector.
Its hard allocation bound is `file_default_copy_threshold()` (P: existing policy
`min(256 MiB, physical RAM / 32)`, not a new budget). Above an injected selector
but within that default bound, memory grows to the complete file size. Above
the default bound, acquisition reports `FILE_ERR_NOMEM` with native
`ERROR_FILE_TOO_LARGE` before allocating bytes or exposing a prefix. An allocator
failure within the bound also fails; there is no mapping fallback. This is a
per-original bound, not an aggregate tab-memory budget.

`ERROR_INVALID_FUNCTION` from watch setup on a classified WSL path becomes
`FILE_WATCH_UNSUPPORTED`, with no successful native registration or new timer.
Unexpected errors and all other servers retain ordinary failure semantics. The
Windows editor port must accept this capability result, omit the watch event,
and retain focus/save/bulk checks. Focus already calls file_check independently
of a watch. Save controllers validate the destination identity on a worker,
and acquisition validates before and after provider bulk reads. Private-copy
snapshot consumers continue reading an immutable original; they do not gain an
automatic pathname check from `file_snapshot_faulted`. The sibling's editor port
must cover any command-level recheck before new bulk operations. A future focused
poll should coalesce worker identity checks off the typing path, with an interval
selected from actual active-session cost/latency measurements; no interval or
new timer is introduced here.

Both the synchronous transaction and the file-object entry point refuse WSL
save targets with `FILE_ERR_UNSUPPORTED` and native `ERROR_NOT_SUPPORTED`.
The transaction refuses before directory/metadata opens and temporary creation.
This covers ordinary saves and Save As over existing, absent and colon-named
share destinations. FORCE cannot bypass the provider policy. Save As to a
Windows destination still uses the original transaction and the edited private
snapshot. No stream-like filename, native legacy rename, in-place write,
unlink-then-rename, or metadata-skipping fallback exists. The save controller's
failed state keeps modified true; private NTFS journal recovery remains available.

`STATUS_NOT_SUPPORTED` joins the existing unsupported stream-query statuses as
an empty stream list. Access denial and genuine I/O errors still fail. This
status change does not authorize WSL saving. Unresolved provider symlinks keep
their native open error and are never replaced. Case and trailing names stay
literal; the tab comparator currently uses strcmp plus actual identity, with
no case-fold comparison. Cross-distro identity uniqueness remains unproved.

The only editor change is `editor_save_overlay` in session.c. It renders the
exact WSL refusal text for the new error and for a failed WSL Save As, retaining
a fresh Windows Save As failure's own notice. The sibling owns the remaining
Windows editor integration. The worker report distinguishes native file/controller
acceptance from the unbuilt Windows editor's end-to-end UI coverage.

The explicit `make TARGET=win -j16 win-wsl-share-degraded` target follows the
probe's private-station/marker-helper/profile-isolation pattern and is outside
all/check. Ordinary Windows check injects measured provider failures on private
NTFS fixtures; Linux all/check requires no WSL path or interop installation.

Explicit `file_watch_poll` also works for an unregistered WSL member: it
coalesces an identity job and installs CHECK_DONE through the existing mailbox.
This supplies a watch-independent async check for the editor port; it does not
schedule itself. Save refusal is deliberately before target I/O, so it does not
itself refresh a copied original's conflict state. The sibling must retain the
separate pre-save and pre-bulk checks required by the brief; the report names
the exact gaps and distinguishes the provider read validation from private
snapshot fault polling. Whole-editor signoff remains partial until those
callers accept the capability result and the native two-tab/UI paths run.
