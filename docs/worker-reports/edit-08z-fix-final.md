Fixed glyph completion so it preserves text-only composition and skips minimap filling. Added regression coverage for caret, blink, focus, input and Unicode-title cases.

Unicode tests, `--minimap-flash` and `make all` pass. The active allocation guard reports zero mallocs across 10,000 keys.

The full `editor_test` was run but fails in IPC/display setup because sandbox sockets return `EPERM`.

Cause, fix and outputs are appended to [edit-08z.md](/home/tobias/Projects/sublimite/.wt/edit-08z/docs/worker-reports/edit-08z.md:211). No git or bd commands were run.