# edit-zzj.13b editor integration decisions

A pending copy open owns two independent trees: a bounded preview and the stable
full-document tree borrowed by tabs/undo. Rendering binds only the preview;
mutating input remains queued until full acquisition and identity-checked
adoption. Adoption runs in maintenance after preview submission and rebinds the
view before destroying the preview. `open_pending` is independent of the frame
pending flag. Mapped acquisition retains main's landed startup contract and
remains explicitly unfinished under finding 8.

Automatic ordinary edit bursts use undo's public contract. Explicit groups
remain for replacements and indentation. A monotonic new-group serial and
incrementally maintained group/eviction counters cross the module boundary;
editor code never inspects private undo records. The editor history ring records
one replacement interval plus repaired selection states per logical group.
Ring eviction advances the head arithmetically, even if many old groups detach.
The new query retains pre-group accounting during sliced replay. Existing full
statistics remain available for diagnostics and tests.

Replay retains one logical group delta while undo's checkpoint spans multiple
turns. Each call admits at most 8 operations (G) with a 0.1 ms deadline (G).
Journal/layout/submit wait until that group completes; blinking/resizing also
wait so provisional tree bytes are not rendered. Individual piece mutations and
large staging/resize/validation remain outside this resumability claim.

Watch descriptors belong to file objects and register with the existing editor
poll descriptor. Notifications request worker identity checks; focus and initial
bulk/adoption validate identity. Stale state cancels jobs, suppresses result
adoption and freezes source-dependent rendering/editing. Reload/keep choices
are maintenance actions: R opens a fresh generation then closes/retains the old
tab; K invokes the file layer's safety policy and retains suspension if unsafe.
A change during replay uses undo_clear to abort its checkpoint and discard that
log rather than continuing over a stale original. The cached viewport retains
its pixels under a minimal source-change status line.

Native allocation attribution is a default integration test around real raw
XCB dequeue followed by production x11_input_key, independent of editor ingress.
Its synthetic allocation calibration proves the interval is counted. Mutation,
layout and submit retain their existing guarded path; XCB packet allocation
retains the existing library exemption. The fuzzer's one-edit model deliberately
spaces event timestamps beyond the automatic burst timeout; same-time grouping
is checked by the default editor regression.

The existing undo benchmark ran once after the hot-path counter change. Its
real-piece and mock-piece rows execute back to back on the shared box, load1
6.46 (M)[AC], BAT0 Not charging. Real-piece p50/p99: 20.936191/38.519449 ms
(M)[AC]; bookkeeping mock p50/p99: 0.765929/1.979554 ms (M)[AC]. Reference
limits: 58/78 ms and 6.3/6.3 ms (G). These are regression diagnostics; no
loaded-box timing certification or new algorithm selection is claimed.
