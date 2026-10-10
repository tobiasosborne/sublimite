# edit-457.19 — session 8 continuation decisions

Keep the WIP font design documented in P4.11b.md: verified content/face bake
identity; advance-based mark positioning; invisible-control property;
bounded positive/negative probes with separate negative storage; exact-key
composition continuation; mailbox-only async discovery adoption with
cancellable publication retry. Font and layout own no new global state.

Wrapped and unwrapped layout preserve the current cold cluster by replaying
it on LAYOUT_MORE. The input-check boundary is the caller's next layout slice.
The integration suite must use this public continuation directly; an external
font-preparation/restart helper would conceal a dropped FONT_MORE result.

Keep append-only atlas storage immutable through T5. Exhaustion is a resource
condition, exposed through font resource_error and layout_approximate. A caller
must replace the cache after T5 or quiescent shutdown, then fully redraw and
retry. The deferred backend integration test exercises actual exhaustion,
delayed T5, retirement, opposite-size replacement and correct-content retry.
Runtime-font ownership in the current ASCII-only editor is separate wiring;
this bead does not implement that ownership or claim automatic retirement.

Discovery cancellation coverage includes both cancellation before execution
and cancellation of a physically complete but unadopted mailbox result.
The latter must leave public done and paths untouched. The session 8 isolated
mutation red restores the premature done side channel; no production source
was temporarily changed for it.

Keep binding benchmark correctness/guard/tail predicates. Interpret the
once-only loaded-box measurements as TRACK, without a timing gate verdict or
repeating runs to chase a pass. Detailed observations and all red/green
evidence are in docs/worker-reports/edit-457.19-s8.md.
