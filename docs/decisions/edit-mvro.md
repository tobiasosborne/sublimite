# edit-mvro — synchronize fixtures with completed operations

The detached-rewrite test must kill its editor only after the self-contained
checkpoint is durable. An expired wait cannot authorize a ready signal. The
checkpoint's empty BASE path is installed by `editor_publish_poll` only after
the worker reports success; continue waiting for that state, and fail locally
if the hang guard expires.

Large checkpoint/replay waits use the OS conformance suite's existing
whole-case hang guard, not `EDITOR_IDLE_NS`. (M) [AC, desktop Spiral]: eight
copies with 32 CPU hogs took up to 18.156 seconds per scenario and exceeded the
old ten-second wait; (E) the reused 120 seconds gives more than six times that
observed duration. This is a hang guard, not a performance requirement.

Mapped-fixture assertions require the full adopted tree. `editor_open` can
return with a copied prefix preview while the worker prepares the mapping.
Wait for `open_pending` to clear before cancelling workers or inspecting the
mapping; preserve the cold-paint fault count and all text/navigation checks.
Other mapped-index fixtures make the same assumption. Wait for adoption
before replacing their indexes, and explicitly reestablish the intended
incomplete-index state if opening has already completed the previous index.

Save As completion is a lasting path adoption (or a refusal notice), not a
requirement to observe `save_as` non-null between turns. Workers can finish
before the next turn observes them. The test still checks file bytes, owner
and peer state, publication and the deferred peer refresh.

No journal format, recovery ordering or production behavior changes.

The existing edit-7cj8 decision explicitly leaves an original-BASE dependency
until change detection and durable publication finish. Killing the child
while its checkpoint is still pending exercises that documented crash window.
The regression's intended post-publication guarantee must wait for publication;
eliminating the earlier window is separate product/recovery scope.
