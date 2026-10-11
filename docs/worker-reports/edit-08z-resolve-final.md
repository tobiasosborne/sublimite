Resolved all three hunks, preserving both minimap and async glyph/font setups. No `git` or `bd` commands ran.

`make all` passed. ASan font, minimap, Unicode, and `--minimap-flash` tests passed. Full editor testing failed on sandbox-denied native display access and a reproduced launcher `IPC_IO` failure.

Report: [edit-08z-resolve.md](/home/tobias/Projects/sublimite/.wt/edit-08z/docs/worker-reports/edit-08z-resolve.md).