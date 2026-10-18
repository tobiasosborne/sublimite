# edit-q0g-s8 — historical keymap per notification (P2-1 §21 remainder)

Context: after a941909 each XKB map notification blocked raw input and asked the
bulk worker to fetch the server's *current* map. When two map replacements (and
keys between them) were already queued before the UI drained, both fetches saw
the final map and the key after the first switch was translated with it
(`map_generations_pending`: expected `zy`, actual `yy`). GetMap has no history
selector (edit-q0g decision), so the map has to be captured when the change
happens, not when the UI reaches it.

Decisions:

- **Persistent listener on the worker connection.** `keyboard_conn` selects the
  same NewKeyboardNotify/MapNotify events and map parts as the UI connection
  (no StateNotify, so modifier presses never wake it). One long-running
  `WORK_BULK` job polls that connection plus a kick eventfd and fetches as soon
  as it sees a notification, independent of how far behind the UI queue is.
- **Exact labels.** A snapshot carries the server time of the newest
  notification received before its replies. A notification received during the
  fetch may precede some replies (torn or stale map), so the listener refetches
  until a fetch completes with none. NKN and MapNotify both carry `time` at
  byte 4; the server stamps one time per change for all clients.
- **Kicks cover what the listener never saw.** Every UI notification writes the
  kick eventfd; a snapshot also carries the number of kicks consumed before the
  fetch began. A boundary is settled by the oldest snapshot whose label is not
  older than the notification's time, or whose kick count reaches the kick
  written at it (synthetic notifications, a selection race at setup, a fetch the
  worker dropped). If snapshots arrived but none covers and none is in the
  mailbox, the UI kicks again, so a boundary can never wait forever.
- **Prepaid slots, N = WORK_DRAIN_MAX_MESSAGES (64).** Snapshots live in
  `x11_runtime.slots[N]` (single producer = listener, single consumer = UI,
  `consumed` is the only shared atomic; the work mailbox message per slot gives
  order and release/acquire). Derivation (P): each occupied slot has exactly one
  undrained message and the listener adds at most one failure message, so
  N + 1 <= WORK_MAILBOX_CAP (256) means publish never fails for capacity
  (`_Static_assert`); N = WORK_DRAIN_MAX_MESSAGES lets one bounded drain
  acknowledge every occupied slot. No count of layout switches is assumed: a
  full ring makes the listener drop its newest fetch, and freeing a slot from a
  full ring kicks a fresh one (degrades to "newest map", never a hang). The UI
  stops polling the pool eventfd while the ring is full.
- **Protocol limit, unchanged in kind:** if a second replacement reaches the
  server before the listener's fetch of the first completes, the first map no
  longer exists anywhere; the key after it uses the newest captured map
  (`map_history` "lost generation" case). The window shrank from "until the UI
  thread reaches the notification" to "one worker fetch".
- **Failure semantics kept:** a listener that stops (connection error, failed
  fetch) only fails `plat_run` when a boundary needs a map it can no longer get.
- Typing path: the UI side adds an eventfd write per *map notification* and
  slot bookkeeping; no allocation. Listener allocations (event polling, XKB
  construction) stay inside the edit-cuh exemption brackets; the release
  `test_rebuild_guard` window (UI boundary + listener fetch) counts 0.
- Tests: `x11_order_test map_generations` (live, private Xvfb, now in `all`)
  waits — liveness bound only — for a pending keymap result after the first
  switch, i.e. the scenario where the worker kept up. `map_history` is
  display-free and drives the slot ring by hand (no listener).
