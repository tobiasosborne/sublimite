# edit-pd6 (s6): wheel or page input during a pending byte jump stops the loop

Worker: Claude Opus. Worktree .wt/edit-pd6-s6. Usage at start: 5-hour 22 %, 7-day weekly 82 %.
Usage at end: 5-hour 25 %, 7-day weekly 83 %.
Power: BAT0 "Not charging" [AC]; load average 23.5 at the red build, 10.2 at the green run.

## Root cause

Finding 13 is confirmed. While `b->scroll.request == SCROLL_BYTE`, `scroll_wheel`
(input.c editor_handle_wheel) and `scroll_key_motion` (input.c page/doc
branch) return SCROLL_ERR_ARG (-1). `editor_step` then calls fail(), and the
error becomes sticky.

## Fix (queue; see docs/decisions/edit-pd6.md)

- src/editor/scrollctl.c adds:
  - `editor_scroll_jumping`, `editor_scroll_queue` and `editor_scroll_cancel_jump`;
  - `apply_queued`, called in `editor_scroll_slice` after the jump resolves and before the follow;
  - queue clearing in seek, place and init;
  - an early return in `editor_scroll_follow` while motion is queued behind a jump.
- src/editor/input.c: a wheel or page during a jump is queued, and the jump and find flags are kept. Doc Home/End cancel the jump and then apply.
- src/editor/private.h: two `editor_buffer` fields and three declarations.
- No allocation was added. The queue is two integers in the buffer.

## Test

tests/editor_scroll_test.c keeps the previous Codex worker's scaffold: held
resident span, malloc guard and argv selectors. Its assertions are replaced.
Each event (wheel notch, PageDown, PageUp, Ctrl+Home, Ctrl+End) runs twice:
- reference: the jump resolves, then the event;
- raced: the event arrives while the jump's resident window is held.

The test asserts:
- the loop keeps stepping with no sticky error and no allocation;
- a wheel or page leaves the jump retained, with no frame yet;
- after release, a frame is submitted;
- the final top, line, cursor and anchor equal the reference;
- a wheel or PageDown ends past the jump target.

The 1 GB fixture was not needed. The held resident window is the
deterministic form of a pending jump.

## Red (pre-fix sources, final test)

```
editor_scroll_test: pending-jump motion returned -1, sticky error -1
editor_scroll_test:198: FAIL rc >= 0 && !e->error && (!edit_malloc_guard_active() || allocations == 0)
editor_scroll_test:222: FAIL jump_then(event, true, &raced) == 0
editor_scroll_test: null fractional/core wheel, overscan, immutable refresh/repeat/missing/stale correlation PASS
editor_scroll_test: held worker cancellation retains snapshot, typing submits, allocations=0 guard=sanitizer-inert PASS
editor_scroll_test: fractional resize rebuilds clip/overscan and preserves origin PASS
editor_scroll_test: wrap-off EOF typing submits while bulk worker is held PASS
exit=1
editor_scroll_test: pending-jump motion returned -1, sticky error -1
editor_scroll_test:198: FAIL rc >= 0 && !e->error && (!edit_malloc_guard_active() || allocations == 0)
editor_scroll_test:222: FAIL jump_then(event, true, &raced) == 0
keys exit=1
```

## Green

```
== editor_scroll_test
editor_scroll_test: null fractional/core wheel, overscan, immutable refresh/repeat/missing/stale correlation PASS
editor_scroll_test: held worker cancellation retains snapshot, typing submits, allocations=0 guard=sanitizer-inert PASS
editor_scroll_test: fractional resize rebuilds clip/overscan and preserves origin PASS
editor_scroll_test: wrap-off EOF typing submits while bulk worker is held PASS
editor_scroll_test: wheel keysym=0 during a pending byte jump matches the unraced result, loop continues, top=206 PASS
editor_scroll_test: key keysym=65366 during a pending byte jump matches the unraced result, loop continues, top=214 PASS
editor_scroll_test: key keysym=65365 during a pending byte jump matches the unraced result, loop continues, top=186 PASS
editor_scroll_test: key keysym=65360 during a pending byte jump matches the unraced result, loop continues, top=0 PASS
editor_scroll_test: key keysym=65367 during a pending byte jump matches the unraced result, loop continues, top=388 PASS
exit=0
```
- `make all`: exit 0.
- Sanitizer suites, run serially with tools/with_display_lock.sh, DISPLAY=:99 and a private XDG_RUNTIME_DIR. Final lines:
  - editor_large_test: "editor_large_test: ok", exit=0
  - scroll_test: "scroll_test: PASS", exit=0
  - editor_test: "editor_test: all passed", exit=0

## Found, not fixed (outside this bead)

1. `editor_scroll_follow`'s second path (view-certified adoption) can store
   `b->scroll` with request SCROLL_RELATIVE and nonzero pending_rows while
   `scroll_pending` is false. This is because `scroll_follow` calls changed() on
   the copy. It was seen after an unraced PageDown. A later relative motion then
   starts from those stale pending_rows. Its effect has not been measured.
2. Arrow and other motions during a jump, with nothing queued, still use the old
   follow paths. The fast path can supersede the jump (pre-existing behaviour;
   no error).
3. Wrapped mode was already safe: `wrap_wheel` cancels its private
   `e->wheel` byte seek. It was not changed.
