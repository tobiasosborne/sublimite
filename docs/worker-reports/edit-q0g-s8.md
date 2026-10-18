# edit-q0g-s8 — worker report

Bead: edit-q0g remainder (P2-1 §21): X11 keys after several keymap switches in
one drain must translate with their own map. Decision: docs/decisions/edit-q0g-s8.md.

## Files touched

- `src/x11/x11.c` — keymap runtime: persistent listener job on the worker
  connection (selects NKN/MapNotify, fetches immediately, refetches if a
  notification lands mid-fetch, labels snapshots by server time + kick count);
  `X11_KEYMAP_SLOTS` prepaid snapshot slots (SPSC, `consumed` atomic, one
  mailbox message per slot); UI `request_keymap(p, time)` + kick eventfd;
  `resolve_keymap` adopts the oldest covering snapshot, discards older ones,
  re-kicks if nothing can arrive; pool eventfd not polled while the ring is
  full; shutdown reclaims every slot after join. `rebuild_keyboard` split into
  `fetch_keyboard` (no publish) used by the listener.
- `tests/x11_order_test.c` — `map_generations_pending` made deterministic (waits,
  liveness bound only, for a pending keymap result after the first switch) and
  promoted into `all` as `map_generations`; new display-free `map_history`
  (runs before Xvfb); `test_map_boundary` drives the slot ring with the listener
  stopped; `test_rebuild_guard` puts the UI boundary + listener fetch inside the
  guard window; `test_live_map_boundary` accepts the listener having captured
  German before the grab (post-change key must still never be US).
- `tests/keyboard_guard_test.c` — calls `fetch_keyboard` instead of `rebuild_keyboard`.
- `docs/decisions/edit-q0g-s8.md`, this report.

## Red (unmodified implementation, test already carrying the wait)

```
$ ASAN_OPTIONS=detect_leaks=1 build/san/tests/x11_order_test map_generations_pending
§21 generations: expected=zy actual=yy keymaps=6
x11_order_test: FAIL §21 exact snapshots retain both successive server mapping generations
x11_order_test: map_generations_pending FAILED
exit=1
```
(Key after the first switch, de, translated with the final US map.) The
display-free `map_history` uses the new slot seam and cannot compile against
the old runtime; the live case above is the behavioural red.

## Green

```
$ ASAN_OPTIONS=detect_leaks=1 build/san/tests/x11_order_test map_generations
x11_order_test: map_generations ok                                  exit=0
$ ASAN_OPTIONS=detect_leaks=1 build/san/tests/x11_order_test map_history
x11_order_test: map_history §21 key after first switch uses the first map layout=de expected=z actual=z
x11_order_test: map_history §21 key after second switch uses the second map layout=us expected=y actual=y
x11_order_test: map_history §21 key after covered boundary layout=de expected=z actual=z
x11_order_test: map_history §21 key after kicked boundary layout=us expected=y actual=y
x11_order_test: map_history ok                                      exit=0
```
`map_boundary` + `map_generations` repeated 5x back to back: all exit 0.

## Verification (Not charging; uptime load 6.41 / 9.08 / 17.60 at the end)

- `make -j3 all` (gcc 13, -Werror): exit 0.
- Sanitizer, `ASAN_OPTIONS=detect_leaks=1`, display suites as
  `XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 EDIT_DISPLAY_LOCK_TIMEOUT=1800 sh tools/with_display_lock.sh ...`:
  x11_alignment_test, x11_clip_review_test, x11_clip_test, x11_display_guard_test,
  x11_font_size_test, x11_identity_test, x11_input_test, x11_live_test,
  x11_order_test (all), x11_stall_test, x11_xi2_test: all exit 0.
- `build/san/tests/keyboard_guard_test` exit 0; release `build/tests/keyboard_guard_test`
  exit 0 (guard active, counted == own probes in all 5 cases).
- `build/san/tests/keys_test` exit 0.
- Release `build/tests/x11_order_test` under the lock: exit 0,
  `x11 keymap rebuild: listener=1 ready=1 counted_allocations=0 guard=1`.
- Release `build/tests/editor_test` on :99 under the lock (malloc guard): exit 0,
  `editor_test: all passed`.

## What remains

- Protocol limit (documented in the decision): when a second replacement reaches
  the server before the listener's fetch of the first finishes, the first map is
  gone; the key uses the newest captured map. Window = one worker keymap fetch.
- No timing measured; none was required.

## Out of scope, noticed (not fixed)

- `xkb_x11_keymap_new_from_device` issues several requests; before this bead a
  change between them could compile a torn map with no detection. The listener
  now refetches in that case; no other caller of that function exists.
- `src/x11/STATUS.md` still describes the historical case as open (not edited:
  outside the bead's listed files; coordinator may update).
