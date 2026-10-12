# edit-vak worker report

Implemented the config hot-reload scroll regression. The original production
resync passes; no production change is retained. Work was confined to this
worktree and this bead; no tracking or version-control commands were run.

## Files touched

- `tests/config_editor_test.c`: file-backed wrapped/scrolled fixture; generation
  adoption checks at submission; wrap-off/on and tab-width reload coverage.
- `docs/decisions/edit-vak.md`: fixture, anchor and submission-check choices.
- `docs/worker-reports/edit-vak-s6.md`: this report.
- `src/editor/preferences.c`: temporarily disabled its resident resync for the
  red probe, then restored byte-for-byte from the original saved source (`cmp`
  passed). There is no final production edit.
- `build/`: generated release/sanitizer products and verification evidence.

## Red run

The test was written before the temporary production fault. Only the
`if (!wrap)` resident-resync block was disabled; geometry-generation adoption
remained enabled. An initial fixture run revealed that full wheel notches
skipped every continuation; the displacement now derives a visual-row step
from the public wheel units. The recorded regression run below then failed at
the preserved logical top in the first submitted reload frame.

Power was `Not charging` (M)[AC], read from BAT0 before verification. All numeric
output pasted in this report is (M)[AC], directly emitted by the named commands;
exit status derives from each process's returned status. No latency, budget or
duration is inferred from these runs.

```text
$ make build/san/tests/config_editor_test
[clang sanitizer build succeeded]
$ ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 build/san/tests/config_editor_test
config_editor_test: FAIL state.first_byte == p->expected_byte && state.first_line == p->expected_line
config_editor_test: FAIL p->seen_generation == editor_preferences_generation(e) && !p->failed
config_editor_test: FAIL result == 0
config_editor_test: FAIL scroll_preferences_test(root, path) == 0
exit=1
```

The disabled source was restored before the green build.

## Green run

```text
$ make build/san/tests/config_editor_test
[clang sanitizer build succeeded]
$ ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 build/san/tests/config_editor_test
config_editor_test: scrolled file wrap off/on and tab-width reloads resync the first submitted frame passed
config_editor_test: DPI startup, size reload/reset/deletion and explicit environment precedence passed
config_editor_test: next frame generation, theme, wrap, tabs, font contract and keymap without typing allocations passed
exit=0
```

The reload hook verifies the expected current/successor frame at generation
adoption, successor geometry identity, logical first byte and line, submitted
origin, layout row starts, line-specific rendered characters after tab
expansion, and exact gutter/label state. Unwrapped checks compare both the owned
logical resident viewport and its displayed copy. Wrapped submission checks its
wrap seed. Each transition is also checked after settling. The scrolled view
remains scrolled through tab-width changes in both wrap modes and the reverse
wrap transition.

```text
$ make -j"$(getconf _NPROCESSORS_ONLN)" all
[gcc release build succeeded]
exit=0
$ DISPLAY=:99 build/tests/config_editor_test
config_editor_test: scrolled file wrap off/on and tab-width reloads resync the first submitted frame passed
config_editor_test: DPI startup, size reload/reset/deletion and explicit environment precedence passed
config_editor_test: next frame generation, theme, wrap, tabs, font contract and keymap without typing allocations passed
exit=0
```

Compiler identities (M)[AC], from `gcc --version` and `clang --version`, were
GCC `13.3.0` and Clang `18.1.3`. The Makefile retained its C language and strict
warning flags. Sanitizer builds enabled AddressSanitizer and UndefinedBehaviorSanitizer.

## Verification limitations and findings outside this bead

LeakSanitizer cannot run under this sandbox's tracing environment. A run with
its default setting emitted:

```text
LeakSanitizer has encountered a fatal error.
HINT: LeakSanitizer does not work under ptrace (strace, gdb, etc)
```

Subsequent runs set `ASAN_OPTIONS=detect_leaks=0`; ASan and UBSan remained
enabled. Leak verification remains for the coordinator outside the sandbox.

The ordinary `make check` built the sanitizer suites but encountered an
already-held shared display lock. The original wait was interrupted; a repeat
with immediate lock failure produced:

```text
$ ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY_LOCK_TIMEOUT=0 make check
base_test: PASS
== build/san/tests/cli_test
waiting for display :99 (held by unknown)
cli_test: display lock timeout: display :99 (held by unknown)
make: *** [Makefile:133: check] Error 1
exit=2
```

The non-display Makefile test list was then supplied to `make check` through
`TEST_SAN`, without editing the Makefile. It stopped at `file_kill_test`:

```text
file_kill_test:198: FAIL rc == 0
file_kill_test:324: FAIL WIFEXITED(status) && WEXITSTATUS(status) == 0
file_kill_test:324: FAIL WIFEXITED(status) && WEXITSTATUS(status) == 0
file_kill_test: FAIL visibility/cancellation; power-loss barriers unverified
make: *** [Makefile:133: check] Error 1
```

That suite compiles `bench/file_bench.c`; its viewport setup calls `plat_init`
and requires X11. The Makefile omits it from its display test list. Reported for
coordinator classification and Xvfb rerun; not changed here.

Remaining suites were run serially so this failure did not hide their results.
`ipc_test` could not initialize its local socket in this sandbox:

```text
init rc=4 errno=1 (Operation not permitted)
FAIL tests/ipc_test.c:130: init_rc==IPC_OK
```

`journal_kill_test` also failed:

```text
journal_kill_test: FAIL trial=0 target=1 issued=1 page_cache=1 ack=0 replay=0 rc=6 error=0
```

Its replay callback accepts only the legacy window payload, while
`journal_set_window` writes the flagged current window payload. This is a stale
fixture outside the config-scroll bead; it was inspected and reported, not
fixed. Consequently a completely green non-display check is not claimed.

## Remaining green verification

The requested scroll suite and the editor's existing sandbox mode passed:

```text
$ ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 build/san/tests/editor_scroll_test
editor_scroll_test: null fractional/core wheel, overscan, immutable refresh/repeat/missing/stale correlation PASS
editor_scroll_test: held worker cancellation retains snapshot, typing submits, allocations=0 guard=sanitizer-inert PASS
editor_scroll_test: fractional resize rebuilds clip/overscan and preserves origin PASS
editor_scroll_test: wrap-off EOF typing submits while bulk worker is held PASS
exit=0
$ ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 build/san/tests/editor_test --headless
[full headless log retained in build/edit-vak-evidence/edit-vak-editor-headless.log]
editor_test: scrolled undo/redo viewport repair passed
editor_test: mutation error is negative and stops the loop passed
editor_test: null script and journal byte model passed
editor_test: null 10000 keys mallocs=0 guard=ASan-inert
editor_test: idle blink timeout and unfocused timer disarm passed
editor_test: queue depth one and coalesced edits passed
editor_test: headless passed
exit=0
```

The ordinary editor suite also ran to its reported failure. Its native backend
open assertions failed because the sandbox lacks access to the X socket, and
its real launcher-wait case failed because local socket setup was denied:

```text
$ ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 build/san/tests/editor_test
reload launcher result=4
editor_test:2260: FAIL waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0
editor_test:1545: FAIL editor_open(&e, &cfg, &b) == 0
editor_test:1571: FAIL editor_open(&e, &cfg, &b) == 0
editor_test:1853: FAIL editor_open(&e, &cfg, &backend) == 0
editor_test:2615: FAIL editor_open(&e, &cfg, &b) == 0
editor_test:3941: FAIL review_suite(headless ? "headless" : NULL) == 0
exit=1
$ ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 build/san/tests/cli_test
cli_test:391: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
cli_test: §36 forced startup timeout correctly failed
cli_test:391: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
exit=1
```

The non-display Makefile run passed `base_test`, `config_editor_test`,
`config_test`, `config_worker_test` and `dataloss_test` before the native
`file_kill_test` stopped it. The serial continuation produced the following
observed process statuses:

```text
build/san/tests/file_test exit=0
build/san/tests/find_bench_test exit=0
build/san/tests/find_bestof_test exit=0
build/san/tests/find_p1r_test exit=0
build/san/tests/find_simd_filter_verify_test exit=0
build/san/tests/find_test exit=0
build/san/tests/find_timeout_test exit=0
build/san/tests/findui_test exit=0
build/san/tests/font_selection_test exit=0
build/san/tests/font_size_test exit=0
build/san/tests/font_test exit=0
build/san/tests/harness_test exit=0
build/san/tests/hl_parse_test exit=0
build/san/tests/hl_test exit=0
build/san/tests/hl_worker_test exit=0
build/san/tests/indent_test exit=0
build/san/tests/ipc_test exit=1
build/san/tests/journal_kill_test exit=1
build/san/tests/journal_test exit=0
build/san/tests/keys_test exit=0
build/san/tests/layout_test exit=0
build/san/tests/lineidx_test exit=0
build/san/tests/minimap_test exit=0
build/san/tests/piece_bounds_test exit=0
build/san/tests/piece_mem_test exit=0
build/san/tests/piece_mt_test exit=0
build/san/tests/piece_test exit=0
build/san/tests/prewake_test exit=0
build/san/tests/raster_scroll_test exit=0
build/san/tests/render_test exit=0
build/san/tests/replay_test exit=0
build/san/tests/runtime_test exit=0
build/san/tests/savectl_reload_test exit=0
build/san/tests/savectl_test exit=0
build/san/tests/scan_test exit=0
build/san/tests/scroll_display_test exit=0
build/san/tests/scroll_test exit=0
build/san/tests/startup_test exit=0
build/san/tests/tabs_test exit=0
build/san/tests/trace_pairs_test exit=0
build/san/tests/trace_test exit=0
build/san/tests/undo_test exit=0
build/san/tests/unicode_render_test exit=0
build/san/tests/utf8_tables_test exit=0
build/san/tests/utf8_test exit=0
build/san/tests/view_test exit=0
build/san/tests/wayland_bench_test exit=0
build/san/tests/wayland_gl_test exit=0
build/san/tests/wayland_test exit=0
build/san/tests/window_size_test exit=0
build/san/tests/work_foreground_test exit=0
build/san/tests/work_test exit=0
build/san/tests/wrap_test exit=0
```

The replay shell contract passed. The display-lock shell contract could not
acquire the already-held shared lock:

```text
$ ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 sh tools/test_replay_cli.sh build
[all replay CLI cases passed]
test_replay_cli: all passed
exit=0
$ ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY_LOCK_TIMEOUT=0 sh tools/test_display_lock.sh contract build/san/tests/refwin_test
waiting for display :99 (held by unknown)
sh: display lock timeout: display :99 (held by unknown)
exit=1
```

Full build and test output is retained in `build/edit-vak-evidence/`, including
the Makefile-derived non-display suite list and serial per-suite logs.

## What remains

The bead's regression and scoped implementation work are complete. The
coordinator must rerun display suites and display-lock contracts on its
specified Xvfb display `:99` with access to local sockets, and rerun with
LeakSanitizer enabled outside the tracing sandbox. The non-display
`journal_kill_test` failure needs separate triage of its stale window-record
expectation; this worker did not fix it. The native `file_kill_test` display
classification omission also remains outside scope. No other changes were
made, and no entirely green `make check` is claimed.
