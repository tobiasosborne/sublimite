# edit-whl: dispatch redo-or-repeat through redo history

Ctrl+Y resolves to `KEYS_ACTION_REDO_OR_REPEAT`; input previously recognized only `KEYS_ACTION_REDO`. Both now use the existing cooperative history replay, preserving selection, journaling, and viewport repair.

Repeat is intentionally absent under the brief's allowed fallback. The editor records grouped deltas and selection snapshots, not a replayable last text command. A delta does not cheaply retain insert bytes or distinguish the original delete command's intent. When no redo entry exists, the existing history guard leaves document bytes unchanged. No command recorder or typing-path allocation was added.

Regression expectations derive from literal ASCII byte lengths. The no-change requirement is (G) the brief's empty-redo fallback; zero typing-path allocations is (G) CLAUDE.md Law 2. See the [worker report](../worker-reports/edit-whl.md) for baseline red and final green runs.
