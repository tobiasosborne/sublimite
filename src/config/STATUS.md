# Config and themes

Implemented a bounded TOML subset visitor and transactional settings parser,
XDG/HOME path resolution, dark/light placeholder token tables, keymap override
lookup, and worker reload through the file module's shared inotify service and
work mailboxes. The editor applies theme, keymap, tabs and wrap before the
containing frame's submit. Font points/family are exposed for the landing
DPI/font consumer.

Parser tests include malformed text, integer/float range errors, UTF-8 and
Unicode escapes, duplicate keys/tables, all supported value types, and every
octet substitution at every byte position of the documented settings seed.
Runtime tests exercise initially absent config, create/rename/delete,
invalid-input preservation and shared parent-watch retirement. The headless
editor test checks the containing frame generation and the release allocation
guard. A libFuzzer entry point and a tracked parser benchmark are included.

Exact red/green output and full build/suite results are in
[the worker report](../../docs/worker-reports/edit-v0o.7.md).
The parser/reload contract is in
[the decision](../../docs/decisions/edit-v0o.7.md).

Remaining dependency integration: reconcile provisional token names with the
missing visual spec; consume points/family in the landing font/DPI code;
coordinator display and leak-sanitizer checks outside this sandbox and the next
Astra comparison drive. No display-latency or final palette approval is claimed.

Final verification: GCC make all passes; config, headless editor config and
worker-cancellation suites pass in release and ASan/UBSan. Every suite was
attempted. Display/Unix-socket denials and the existing editor stale-minimap
assertion keep full make check non-green; exact failures and the successful
large-editor headless rerun are in the worker report. Leak scanning needs the
coordinator outside the sandbox.
