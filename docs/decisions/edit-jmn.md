# edit-jmn: CLI tab reservation and handoff diagnostics

The application reserves its tab table during startup. The live-tab bound is
1000 (G), taken directly from the bead's Sublime-comparable target. The editor
library's existing configurable default is unchanged; main supplies the larger
reservation. This preserves stable tab addresses and adds no allocation or
frame deferral to typing, switching, or tab publication.

The existing tabs census measures 4800 bytes (M)[bat] per `tabs_tab`; the order
and free-slot arrays add `2 * sizeof(uint32_t) = 8` bytes (M)[bat], giving
4808 bytes (M)[bat] per reserved slot. Its retained-closed allowance is 16
(existing census input). Thus the application target requires
`(1000 + 16) * 4808 = 4884928` bytes before page rounding;
`ceil(4884928 / 4096) * 4096 = 4886528` bytes for the arena, and adding
`sizeof(tabs_set) = 128` gives 4886656 bytes owned (M)[bat]. Page size and
layout are verified by the actual `tabs_init` census, with BAT0=Discharging.
These are tab-control bytes, excluding file data, editor-buffer state, indexes,
and the buffer-owned history/undo mappings. They are not a total memory budget.

IPC status diagnostics now use static descriptive strings. `IPC_REJECTED`
means the receiving editor refused the request; the wire status does not carry
its underlying file or allocator error. Do not claim an exact underlying cause
from that status alone.

The acceptance regression invokes main's real secondary-launch path against
the real editor IPC pump and checks every tab's path, selectability and bytes.
Its workload is 200 unique existing files (G), the bead's acceptance, equal to
twice Astra's 100-command workload. Listener-free probes separately check the
application reservation and target-sized new-path admission.

The actual existing-file rejection reproduced in the listener-free probe is
`EMFILE` from allocating an inotify instance per file. The probe accepted 49
and failed on attempted open 50 (M)[bat], derived from its loop index and
saved errno. An independent allocation probe found 53 available instances
(M)[bat], counting successful `inotify_init1` calls until `EMFILE`. Availability
varies with other processes using the same user's kernel quota. The kernel
per-user ceiling is 128 (M)[bat], read from
`/proc/sys/fs/inotify/max_user_instances`; therefore it is not a live IPC-token
ceiling. File-watch sharing belongs in `src/file/file.c`, which this worker's
explicit scope excludes. No watcher change or kernel-limit change was made.

The full CLI acceptance remains unverified and the bead remains incomplete.
The sandbox denies private Unix listener binding as well as the X socket.
The coordinator must resolve the watcher allocation and rerun the acceptance
and display suites outside this sandbox before closing the bead.
