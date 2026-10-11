# edit-v0o.7 second rebase resolver report

Resolved the conflicts in `src/editor/editor.h`, `src/editor/open.c`, and
`src/main.c` on 2026-10-11. No `git` or `bd` command was run. Rebase
bookkeeping remains with the coordinator.

## Resolution and font integration

- Retained main's display query, DPI/point-size validation, work-area/window
  geometry, startup atlas trace, raster fallback, and 1000-tab startup reserve.
  Native startup also enables the branch's preferences runtime.
- Retained main's session lock initialization, recovery-aware journal flush,
  session close, empty-placeholder disposal, and lock release. Config runtime
  shutdown joins its worker before releasing storage. Both the session lock
  and preferences watch descriptor initialize to -1.
- Retained both sets of `editor_config` fields, including font/display sizing
  and preferences loading/path selection. Existing containing-frame application
  of themes, keymaps, tabs and wrap remains in place.
- Initial config reading/parsing stays on its worker. Startup receives the
  snapshot before building the DPI atlas and feeds file `font.size` into the
  editor's `cfg.font_points`. Explicit startup points (the native
  `EDIT_FONT_SIZE_PT` seam) override the file at startup and on later reloads.
- Size reloads request the existing font atlas worker and update the zoom/reset
  baseline; removal/deletion restores the explicit override or default. Atlas
  adoption retains edit-m3g's frame/mutation guards and prepaid size bound.
  Legacy pixel sizing remains supported. Family selection remains pending.

Precedence and font-specific application timing are documented in
`docs/decisions/edit-v0o.7.md`; `docs/config.md` now describes the integrated
size consumer. `tests/config_editor_test.c` adds behavioral coverage for the
startup DPI atlas, reload/reset/deletion, and explicit startup override across
reloads. The release integration test also exercises its existing allocation
guard. No unrelated editor/save/recovery behavior was changed.

## Verification

Complete build/test logs are in `build/config-resolve2-verification/`.

Both the initial and final `make all` runs exited 0 with GCC. Final output is
in `make-all-final.log`; the first run is in `make-all.log`. Project sources
use `-std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion`. The sanitizer build
also exited 0, using Clang with those flags plus
`-fsanitize=address,undefined -fno-omit-frame-pointer`; see `make-san.log`.
The Makefile's existing vendored tree-sitter exception removes only shadow
and conversion warnings for that vendor object. A Clang double-to-bool warning
caught during integration was corrected with explicit comparisons to zero
before the successful final builds.

Commands:

```sh
make all
make build/san/tests/config_test build/san/tests/config_worker_test build/san/tests/config_editor_test build/san/tests/editor_test build/san/tests/font_test build/san/tests/font_size_test
build/tests/config_editor_test
ASAN_OPTIONS=detect_leaks=0 build/san/tests/config_test
ASAN_OPTIONS=detect_leaks=0 build/san/tests/config_worker_test
ASAN_OPTIONS=detect_leaks=0 build/san/tests/config_editor_test
ASAN_OPTIONS=detect_leaks=0 build/san/tests/font_test
ASAN_OPTIONS=detect_leaks=0 build/san/tests/font_size_test
ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --clipboard
ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --ck4-save
ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --minimap-flash
ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY_LOCK_TIMEOUT=0 sh tools/with_display_lock.sh build/san/tests/editor_test
ASAN_OPTIONS=detect_leaks=0 EDITOR_REVIEW_ONLY=p4-16 build/san/tests/editor_test
```

All sanitizer runs disabled only leak scanning. No executed suite printed an
ASan or UBSan diagnostic. Source/test conflict-marker scans are clean, as is
the workspace scan excluding build output and rebase metadata.

| Run | Exit status | Result |
| --- | --- | --- |
| `make all` (final) | 0 | GCC release library, application, tools, tests and benches built. |
| Requested sanitizer binaries plus `font_size_test` | 0 | Built with ASan/UBSan. |
| Release `config_editor_test` | 0 | Existing allocation/frame checks and new DPI/precedence integration passed. |
| ASan `config_test` | 0 | Parser mutations, settings/keymap and shared-watch reload coverage passed. |
| ASan `config_worker_test` | 0 | Cancellation/transactional parse coverage passed. |
| ASan `config_editor_test` | 0 | Config frame/application checks and new font integration passed. |
| ASan `font_test` | 0 | All font cases passed. |
| ASan `font_size_test` (additional) | 0 | DPI, zoom/reset, retained atlas, shutdown, prepaid bounds and minimap geometry passed. |
| ASan `editor_test --clipboard` | 0 | Round trip, bulk, middle click, PRIMARY and empty-tab cases passed. |
| ASan `editor_test --ck4-save` | 0 | Save and abrupt-exit recovery passed. |
| ASan `editor_test --minimap-flash` | 0 | Stale-frame typing probe passed. |
| ASan full `editor_test`, display lock on `:99` | 1 | Executed; headless launcher-wait and native setup failures below. |
| ASan isolated `EDITOR_REVIEW_ONLY=p4-16` | 1 | Reproduced the headless launcher-wait failure. |

## Remaining full-editor failures

The full run acquired the shared display lock and executed the binary. Its
headless prefix passed save/startup discovery, COPY/mapped Keep/Reload recovery,
clipboard, journal credit/metadata/FIFO, and many independent review cases.
It then reported:

```text
reload launcher result=4
editor_test:1350: FAIL waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0
editor_test:741: FAIL editor_open(&e, &cfg, &b) == 0
editor_test:767: FAIL editor_open(&e, &cfg, &b) == 0
editor_test:1049: FAIL editor_open(&e, &cfg, &backend) == 0
editor_test:1705: FAIL editor_open(&e, &cfg, &b) == 0
editor_test:2586: FAIL review_suite(NULL) == 0
```

The four failed native opens use the CPU backend and do not enable preferences.
A direct connection probe to `/tmp/.X11-unix/X99` returned errno 1,
`Operation not permitted`, establishing the sandbox display-access restriction.
These native cases require a run with access to the supplied Xvfb display.

The separate headless `review_reload_identity` case uses the null backend,
provides an isolated IPC server, and also leaves preferences disabled. Its
launcher returns `IPC_IO` (enum value 4) after the tab-close path. The isolated
review run reproduces it. This failure is not attributed to the display
restriction or established as a pre-existing regression: no main-only baseline
was obtained, and git use was prohibited. Its root cause remains unresolved;
the resolver did not change the unrelated launcher/tab-close path.

The resolver changes build and the requested focused probes pass. Full editor
acceptance remains incomplete because of the native-access restriction and the
headless launcher-wait failure. Exact outputs and result manifests are under
`build/config-resolve2-verification/`.
