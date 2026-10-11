# edit-cx8 law-2 accounting on real backends

The raster G1 rows' `allocations=N structural=FAIL` were not our typing path.
Backtraces taken inside the counting hook (temporary instrumentation, removed)
put every counted call on a worker thread, never the UI thread:

- `src/raster/raster.c` `fence_job_fn`: libxcb `xcb_poll_for_reply` /
  `xcb_poll_for_special_event` malloc each fence reply and Present event they
  read. That job retires frame N (T5/T6) while the UI thread already has key
  N+1's ingress -> submit window open, and the guard in `src/base` is
  process-wide.
- `src/x11/x11.c` `rebuild_keyboard`: libxkbcommon-x11/libxcb constructing a
  new keymap after a server MapNotify. On the shared Xvfb :99 the trigger was
  another worktree's test (`tests/x11_order_test.c` runs `setxkbmap -display
  :99`).

Both are library allocations off the input -> submit path, which the P2.0
addendum already exempts. They are not filtered by thread (that would also
hide our own worker code) or by turning the guard off. A thread-local,
nesting exemption (`edit_malloc_guard_exempt_begin/end`) brackets exactly the
library calls on the thread that makes them; our code in those jobs (post,
publish) and every other thread keep counting. Under sanitizers the calls are
no-ops like the rest of the guard.

The off-by-one completion (`invalid=1 incomplete=1 completed=N+1
expected=N`) is foreign input. A key from another X client delivered to the
bench window takes an input sequence the bench never injected. Every later
sequence shifts by one. Reproduced on a private Xvfb with one `xdotool key x`
during the raster row. The bench now counts such sequences (`foreign_input=`
in the G1 and STRUCTURAL lines). `make bench` runs display benches under
`tools/with_display_lock.sh`, as `make check` already does for display tests.
