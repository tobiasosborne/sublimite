# edit-9lf — editor loop and tabs

Public activation and navigation use one busy predicate covering both the view
continuation and sliced undo replay. Public byte reads and length query borrow
`undo_replay_snapshot` during replay. Layout and journal publication retain the
existing replay boundary; no undo implementation is changed.

Decoded file results remain owned by their file and operation generation.
Installed buffers retain status, errno, kind and generation. Save results also
match the existing per-buffer save generation. Preparation still uses its
separate opening error. Deferred open failure suspends its owner; unrelated
buffers remain editable.

Pending COPY mutations are stamped with logical buffer ID and source generation
at ingestion. Queue dispatch can bypass parked mutations for tab/window controls.
Parked mutations resume only in their original live generation; closed/stale
owners discard them. Ordinary head dispatch remains constant time. Bypassing a
parked entry moves at most the fixed input reservation. No heap allocation is
introduced. Shortcut classification uses a copy of the key-chord state.

Suspension freezes source-dependent actions. Tab keys, releases, focus/resize,
and tab-strip pointer actions remain available. The source banner occupies the
first text row when a tab strip exists, keeping the strip visible.

Recovery key admission requires the prompt generation, fresh ingress serial and
an event timestamp newer than the submitted prompt, with no modifiers/repeat.
Activation of a suspended tab invalidates its earlier prompt admission time;
a newly submitted banner establishes a fresh cutoff. Explicit API choices are also bound to the owning ID/generation, so switching
cannot resolve a different source.

`tabs_replace` swaps borrowed tree/undo/view within the existing tab record,
preserving identity, order and MRU. Editor reload transfers wait membership and
clamps retained view bytes to the replacement. It does not close the logical tab
or add a closed-stack member. Controller-driven silent clean reload is not wired;
journalled reload remains suspended rather than changing an unreconciled BASE.
Those integration pieces depend on the save/journal work. Replacement setup and
whole-source wait are still synchronous and are not claimed as sliced reload.

Eviction cancels/unbinds known large-file handles, cancels the index, unregisters
its watch, and retains the full buffer lease. Nonfile storage is destroyed only
when known handles and index retirement are ready, at most one per maintenance
turn. The public file API offers blocking `file_close` but no nonblocking
retirement/readiness split. File buffers therefore remain leased until explicit
shutdown. The fixed buffer reservation can exhaust: admission checks available
storage before preparing or publishing a new request. This avoids UI waits and
unsafe guesses at private handles, but is an incomplete physical reclamation
solution, with retained memory/capacity costs.

The requested dc4015e bounded undo query returns group counts/serial/evictions;
it exposes neither record count nor memory accounting. Replacement admission
still needs retained record count. No private undo fields are read by production
code and no undo API is added under the API-use-only boundary. The opt-in
`p4-27` regression protects retained record pages in a child and requires
admission refusal to touch none. It remains red until that dependency exists.

Pointer capture records stable tab identity. Activation, focus and resize cancel
capture; dragging resolves the current order index before reorder. Minimap
capture additionally matches source generation. A pointer press establishes its
capture after selection, so normal dragging survives its own activation.

Final tab chrome is prepared in the existing aligned decoration scratch after
decoration work ends. Compare after the stale marker is applied; copy and mark
only changed final cells. The standalone tabs renderer's row-damage contract is
unchanged. This adds no allocation and stays within the existing scratch reserve.

## Paired chrome diagnostic

A build-only copy of the existing editor benchmark selects its null backend;
production benchmark sources are unchanged. Both variants use the final
control/replay behavior. The baseline restores only unconditional tab chrome
rendering; the candidate compares final chrome. They ran back to back within the
same second on the shared AC box (M)[AC], BAT0 Not charging, load1 2.97 (M)[AC],
using `/tmp/edit-corpus/log_1g.txt`, serial alternating insertion/backspace at
byte 966367641 (M)[AC]. Each has 128 samples (M)[AC], below the existing 10000
sample requirement (G), explicitly TRACK.

| Variant | Injection-to-T4 p50 / p99 ns (M)[AC] | Typing allocations (M)[AC] | Structural result |
| --- | --- | --- | --- |
| Unconditional chrome | 130385 / 167413 | 0 | OK |
| Compared final chrome | 134162 / 144132 | 0 | OK |

No timing gate is certified and no native raster/upload latency is measured.
The choice rests on the deterministic regression eliminating unchanged tab-row
damage; the paired diagnostic finds no typing allocation regression. Full
trace/performance adjudication remains coordinator work.
