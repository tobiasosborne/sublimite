# edit-2ts rebase conflict resolution

Resolved the four conflicted files without running `git` or `bd`. The rebase
metadata and index are left for the coordinator to manage.

## Changes retained

- `src/trace/trace.h`: pipeline IDs remain 0–6 and main's composition IDs
  remain 7–8. Startup IDs append in branch order at 9–20; `TRACE_EV_COUNT`
  is 21. Startup retention and `tracedump` reporting use that appended range.
- `src/render/render.h`: retained main's `render_pixels` region and
  `max_region_width`/`max_region_height`, plus startup's `background_init`.
- `src/editor/private.h`: retained main's clipboard action/type/helpers,
  session/recovery fields, minimap pixel buffers, and startup's separate pool,
  initializer state, retired backend, flags, and helpers.
- `src/editor/open.c`: retained clipboard and recovery setup/teardown and
  minimap pixel allocations/reservations. Bootstrap initialization is followed
  by the independent GPU probe; its config carries the minimap region reserve.
  Close stops clipboard jobs and joins the probe before backend/platform/arena
  teardown. Successful adoption retains raster storage until close.
- Main's `src/gl/gl_driver.h` completion message still publishes
  `.generation = c->generation`; `gl_test` native capability gating is retained.
  The branch's `--require-gl` fixture still waits for final backend selection.
- `tests/startup_test.c` now asserts the stable trace ID boundary and verifies
  composition events round-trip as frame records outside retained startup stamps.

The referenced `docs/worker-reports/edit-2ts.md` is absent in this checkout.
Startup behavior was checked against `docs/decisions/edit-2ts.md`,
`docs/worker-reports/edit-2ts-s2-final.md`, existing startup logs, and
`tests/startup_test.c`.

## Validation

Final scan of `src/` and `tests/`: no conflict markers remain.

`make all` exited 0 with GCC and the Makefile's
`-std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion` flags.
[Build log](edit-2ts-resolve-make-all.log).

`make build/san/tests/editor_test build/san/tests/startup_test
build/san/tests/trace_test build/san/tests/render_test` exited 0 with Clang
ASan/UBSan and the same warning flags.
[Sanitizer build log](edit-2ts-resolve-make-san.log).

All sanitizer runs below used `ASAN_OPTIONS=detect_leaks=0`. Editor/display
tests used `DISPLAY=:99 sh tools/with_display_lock.sh`; leak checks were
disabled as requested. No ASan/UBSan diagnostic was reported in these runs.

| Test | Exit | Evidence |
| --- | --- | --- |
| `build/san/tests/editor_test` (no arguments) | 1 | [Full editor log](edit-2ts-resolve-editor-all.log); failures detailed below |
| `editor_test --minimap-flash` | 0 | [Flash log](edit-2ts-resolve-editor-minimap-flash.log): 95 keys, zero orange frames, zero pending pixel changes, zero allocations |
| `editor_test --clipboard` | 0 | [Clipboard log](edit-2ts-resolve-editor-clipboard.log): round trip, replacement, undo, bulk/journal, PRIMARY and no-tab cases |
| `editor_test --ck4-save` | 0 | [Recovery log](edit-2ts-resolve-editor-ck4-save.log): abrupt exit and selected/unrelated/closed buffer recovery |
| `build/san/tests/startup_test` | 0 | [Startup log](edit-2ts-resolve-startup_test.log): held failing/successful probe, painting and typing before release, adoption, late wake drain, retained stamp wrap/reset and composition round trip |
| `build/san/tests/trace_test` | 0 | [Trace log](edit-2ts-resolve-trace_test.log) |
| `build/san/tests/render_test` | 0 | [Render log](edit-2ts-resolve-render_test.log) |

The full editor run completed in approximately 134 seconds, failed the
reload/launcher fixture at `tests/editor_test.c:1350` (`IPC_IO`, result 4),
and failed native raster editor setup at lines 741, 767, 1049, and 1705.
`review_suite(NULL)` consequently failed at line 2555, stopping the remainder
of the default suite. The earlier branch suite logs also report the
reload/launcher failure. This assertion remains unresolved; no unrelated IPC
code or test assertions were changed for this resolution.

A direct AF_UNIX connection to `/tmp/.X11-unix/X99` fails with errno 1,
`Operation not permitted`, in this sandbox. Native display acceptance could
not be established here. [Connection evidence](edit-2ts-resolve-environment.log).
Headless recovery, clipboard, minimap and startup tests above passed.

Supplemental checks:

- `editor_test --gpu-completion` exited 0:
  [GPU completion log](edit-2ts-resolve-editor-gpu-completion.log).
- `editor_test --fallback` and `editor_test --require-gl` each exited 1 at
  native `editor_open` setup, before they could exercise native backend
  selection. [Fallback log](edit-2ts-resolve-editor-fallback.log),
  [require-GL log](edit-2ts-resolve-editor-require-gl.log).
  The headless startup fixtures independently verify probe failure and success.
- `make build/san/tests/gl_test` and the sanitizer `gl_test` run each exited 0.
  Snapshot/completion/trace checks passed; native diagnostics explicitly
  skipped for unavailable Xvfb/EGL context.
  [Build log](edit-2ts-resolve-make-gl-san.log), [GL log](edit-2ts-resolve-gl_test.log).
- Release `build/tests/startup_test` exited 0 with its allocation guard active
  and zero allocations during typing while each probe was held.
  [Release startup log](edit-2ts-resolve-startup-release.log).
- `build/tools/tracedump build/startup.trace` exited 0; its startup section
  contains the new stages and elapsed-time arithmetic. The trace came from
  the headless startup harness, not an OS launch measurement.
  [Tracedump log](edit-2ts-resolve-tracedump.log).

Machine-readable exit codes and timings:
[requested module tests](edit-2ts-resolve-unit-results.json),
[editor runs](edit-2ts-resolve-editor-results.json).
