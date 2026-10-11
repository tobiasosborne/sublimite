# edit-riu worker report

Implemented in the supplied `wt/edit-cx8` worktree. No `git` or `bd` commands
were run. The supplied edit-cx8 changes are retained.

Evidence convention: file:line coordinates below come from `nl -ba` on the
final source; section labels come from the existing suite. Compiler flags are
copied from the Makefile, not new thresholds. Probe results are (M)[AC]:
`/sys/class/power_supply/BAT0/status` read `Not charging` before verification.
Display strings in probe output are opaque fixture identifiers deliberately
different from the repository's shared-display default, not measurements.

## Change and display derivation

- `tests/x11_order_test.c:501`: `save_server_map()` captures and owns a copy of
  `getenv("DISPLAY")` after Xvfb setup. The snapshot connection explicitly uses
  that string, as do every `setxkbmap` and `xkbcomp` command.
- `tests/x11_order_test.c:554`: `set_layout()` passes the captured display with
  `-display`; clears inherited variants/options for the fixture. Removed the
  literal shared-server target.
- `tests/x11_order_test.c:461`: external tools run as argv arrays through
  `fork`/`execv`; interrupted `waitpid` calls retry. No shell interpolation.

The display is obtained as follows:

- `tools/with_display_lock.sh:17` reads the incoming environment and applies
  its safe-display policy; `tools/with_display_lock.sh:36` exports the selected
  `DISPLAY` before executing the test.
- `tests/x11_order_test.c:802` calls `xvfb_start()` before dispatching tests.
  `tests/x11_xvfb.h:22` reuses the explicitly requested shared server when its
  environment conditions match. Otherwise it starts Xvfb with `-displayfd`,
  reads the assigned display, and exports it at `tests/x11_xvfb.h:52`.
- `src/x11/x11.c:328` connects the platform with `xcb_connect(NULL, ...)`;
  `src/x11/x11.c:191` does the same for its keyboard worker. Neither the
  platform nor the test changes `DISPLAY` during a live-map fixture, so the
  captured value is the display they connect to, including host/screen spelling.

## Restore and skip paths

`tests/x11_order_test.c:501` first probes `setxkbmap -query` on that display,
then saves the complete `_XKB_RULES_NAMES` property and an `xkbcomp -xkm`
snapshot in a uniquely created temporary file. Rules alone would not preserve
a custom map. If querying or saving is unavailable, it prints a `SKIP §21`
line and releases resources before changing the server. Failure to initialize
the US fixture also prints `SKIP` and restores the snapshot, covering a
partially applied command.

`tests/x11_order_test.c:480` reloads the saved compiled map into the same
display, restores the rules property byte-for-byte (or deletes it if originally
absent), and removes the temporary file and saved connection. Restore errors
set the test failure flag. It restores the caller's original keymap rather than
assuming US was the original layout.

`tests/x11_order_test.c:559` and `tests/x11_order_test.c:597` route both live
fixtures through explicit cleanup. Failed platform initialization goes directly
to restore; failed later layout commands go through platform shutdown and
restore. Assertion failures are nonfatal under the suite's `CHECK` macro and
reach the same cleanup. The delayed-worker server grab is released before
shutdown and restoration. Dirty state is set before any layout change.

`tests/x11_order_test.c:805` registers idempotent exit cleanup after the Xvfb
exit hook, so fallback restoration runs before Xvfb is stopped. Explicit
fixture cleanup makes restore failures affect the final test result. Synthetic
map-boundary assertions do not depend on the external tools and remain enabled.

## Verification

The existing live §21 assertions are the display regression. Their red/green
display runs were **not performed**: this sandbox has no X socket, as stated in
the brief. Those runs are for the coordinator.

A temporary headless argv probe extracted the actual layout helper and stubbed
process execution, without contacting X:

```text
before: display argv probe: DISPLAY=probe-host/unix:314.screen target=:99 FAIL
after:  display argv probe: DISPLAY=probe-host/unix:314.screen target=probe-host/unix:314.screen PASS
```

A temporary cleanup probe extracted the final keymap helpers, used stub XCB
transport and stub external executables, and passed normal restoration, partial
US/German command failures, unavailable query/snapshot tools, present/absent
rules metadata, temporary-file removal, and repeated fallback cleanup:

```text
x11_order_test: SKIP §21 live server maps (setxkbmap unavailable on mock-private-display)
x11_order_test: SKIP §21 live server maps (cannot save original map on mock-private-display)
headless keymap cleanup probe: PASS
```

These probes validate argument selection and helper cleanup; they do not claim
live XKB notification or server-map correctness. Their sources and extracted
helpers are in `/tmp/edit-riu-check/` on this machine.

`make all`: **PASS**, including recompiling and linking the changed order test
with GCC and the repository warning flags. A subsequent invocation reported
`Nothing to be done for 'all'` and successful exit.

`make build/san/tests/x11_order_test`: **PASS**, compiled and linked with
Clang and `-fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g`
from `Makefile:14`, plus its normal C/warning flags and forced display guard.
The subsequent invocation confirmed the binary was up to date and exited
successfully. Build logs are `/tmp/edit-riu-check/make-all.log` and
`/tmp/edit-riu-check/sanitizer-build.log`.

No display suite or benchmark was run. Coordinator: run the sanitizer order
suite on the shared display under `tools/with_display_lock.sh` and with a private
Xvfb; check live `map_boundary`, snapshot restoration from a non-US/custom map,
and unavailable-tool `SKIP`. The opt-in `map_generations_pending` remains the
pre-existing exact-generation reproduction; this bead only fixes its fixture
display and cleanup.

## External-tool audit and existing findings

Searched `tests/x11_*.c` for `-display`, `DISPLAY`, `setxkbmap`, and process/shell
invocations. No other external X client invocation was present. The shared
`tests/x11_xvfb.h:36` invocation creates a server with `-displayfd` and exports
its assigned display; it is not a client targeting another server.

Existing, outside the requested external-tool scope: direct transport in
`tests/x11_clip_review_test.c:11` still calls `xcb_connect(":99", NULL)`, and
`tests/x11_identity_test.c:23` requires the shared-display spelling.
`tests/x11_display_guard_test.c:23` deliberately uses literal display strings
to test the guard policy. Reported for coordinator judgment; not changed.

Design record: `docs/decisions/edit-riu.md`. Worklog and handoff contain the
worker verification boundary. Worker stopped after this report.
