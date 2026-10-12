# edit-5ih s6 worker report

Implemented the remaining config consumer and native launch flags. Config
font family/file now request the landed worker preparation and frame-boundary
swap at startup and on inotify reload. Unchanged requests do nothing;
changed requests survive an outstanding selection. Source precedence is CLI,
then nonempty environment, then config, then embedded. The winning source is
selected as a whole pair, with file over family within that pair. The exact
precedence table and launch-only IPC behavior are in
`docs/decisions/edit-5ih.md`.

## Files touched

- `src/config/config.h`, `src/config/settings.c`: file key and scalar-entry bound.
- `src/editor/editor.h`, `src/editor/private.h`: startup overrides and fixed request storage.
- `src/editor/preferences.c`, `src/editor/open.c`: source snapshot, config consumption and deferred-request retry.
- `src/ipc/ipc.h`, `src/ipc/args.c`, `src/main.c`: owned CLI options and native mapping.
- `tests/font_selection_test.c`, `tests/font_cli_test.c`, `tests/config_test.c`: rendering, frame boundary, busy reload, precedence, parser and allocation regressions.
- `docs/config.md`, `docs/decisions/edit-5ih.md`, this report.

No changes to input.c, session.c, Makefile or the prohibited coordination docs.
No bd or git commands were run. Build/test logs are local artifacts in build.

## Red run (before implementation)

```sh
make build/tests/font_selection_test
build/tests/font_selection_test
```

```text
font_selection_test:86: FAIL e->stats.font_changes == changes + 1u
font_selection_test:308: FAIL config_selection() == 0
font_selection_test: family/file parsing, precedence inputs, TTC index and bounds passed
font_selection_test: proportional design advances rejected despite equal rounded pixel widths
```

```sh
make build/tests/font_cli_test
build/tests/font_cli_test
```

```text
font_cli_test: FAIL PARSE(family, &args) == IPC_OK && !args.request.count
```

Both exited unsuccessfully as intended. (P) diagnostic line numbers derive
from the source location macro, not workload counts. The selection assertion
expects the prior adoption count plus the changed config request.

## Green release run

```sh
make -j"$(getconf _NPROCESSORS_ONLN)" build/tests/font_selection_test build/tests/font_cli_test build/tests/config_test build/tests/config_editor_test
build/tests/font_selection_test
build/tests/font_cli_test
build/tests/config_test
build/tests/config_editor_test
```

```text
sublimite: invalid font selection; using embedded DejaVu Sans Mono
sublimite: font selection failed; using embedded DejaVu Sans Mono
sublimite: font selection failed; using embedded DejaVu Sans Mono
font_selection_test: family/file parsing, precedence inputs, TTC index and bounds passed
font_selection_test: proportional design advances rejected despite equal rounded pixel widths
font_selection_test: config startup/reload changes submitted glyph pixels, unchanged family skips rebuild, removal restores default, typing mallocs=0
font_selection_test: frame boundary, input sequences, worker identity, mallocs=0 guard=active
font_selection_test: TTC face selection, geometry recomputation and single adoption passed
font_selection_test: DPI 96 selected Unicode pixels, zoom/reset, glyph retirement, runnable input and outstanding selection shutdown passed
font_selection_test: DPI 243 selected Unicode pixels, zoom/reset, glyph retirement, runnable input and outstanding selection shutdown passed
font_selection_test: environment, installed family, failure fallback, proportional rejection, file precedence and restore passed
font_cli_test: family/file options, required values and option terminator passed
config_test: TOML types, malformed input and exhaustive single-byte mutations passed
config_test: absent file, inotify create/rename/delete, invalid reload and shared watch lifetime passed
config_test: parser, settings and keymap passed
sublimite: font selection failed; using embedded DejaVu Sans Mono
config_editor_test: DPI startup, size reload/reset/deletion and explicit environment precedence passed
config_editor_test: next frame generation, theme, wrap, tabs, font contract and keymap without typing allocations passed
```

All commands succeeded. The concurrency argument (P) derives from the
operating system's online processor count. Test fixture dimensions are reused
from the existing font-selection fixture. (P) required typing allocations are
zero, from CLAUDE Law 2; the release allocator guard is active. DPI values in
these logs are inherited test inputs, with derivations in the landed suite.
Sanitizer execution does not supply allocation evidence because its guard is
explicitly inert.

```sh
make -j"$(getconf _NPROCESSORS_ONLN)" all
```

`make all` succeeded with the repository's gcc release flags. The full log is
`build/edit-5ih-make-all.log`.

The final test additionally waits for the independently scheduled bulk-lane
diagnostic, rather than assuming it finishes with the foreground atlas job.
The final combined `make all` and sanitizer font-test build also succeeded:
`build/edit-5ih-final-build.log`.

## Green sanitizer runs and full-check limitations

```sh
make -j"$(getconf _NPROCESSORS_ONLN)" check
```

The default `make check` built all sanitizer binaries, then stopped in
`base_test` because LeakSanitizer cannot operate under sandbox ptrace:

```text
clang -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -pthread build/san/tests/x11_xi2_test.o build/san/libedit.a -lm -ldl -lwayland-client -lwayland-egl -lxcb-randr -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/san/tests/x11_xi2_test
== build/san/tests/base_test
==639==LeakSanitizer has encountered a fatal error.
==639==HINT: For debugging, try setting environment variable LSAN_OPTIONS=verbosity=1:log_threads=1
==639==HINT: LeakSanitizer does not work under ptrace (strace, gdb, etc)
make: *** [Makefile:133: check] Error 1
```

With `ASAN_OPTIONS=detect_leaks=0`, ASan and UBSan stay active. An ordinary
check then waited for another display-lock owner; I interrupted that wait and
used an immediate, nonblocking lock attempt (P: timeout zero means no wait,
not an invented duration budget):

```sh
ASAN_OPTIONS=detect_leaks=0 EDIT_DISPLAY_LOCK_TIMEOUT=0 make check
```

```text
base_test: PASS
== build/san/tests/cli_test
waiting for display :99 (held by unknown)
cli_test: display lock timeout: display :99 (held by unknown)
make: *** [Makefile:133: check] Error 1
```

A read-only socket probe also confirmed that connecting to the Xvfb socket
`/tmp/.X11-unix/X99` returns `EPERM` in this sandbox. Native display suites
cannot be verified here. The native part of `editor_close_test` and the native
viewport/queued parts of `file_kill_test` fail at platform initialization.
The coordinator must re-run the display suites on Xvfb :99, under the normal
shared display lock, and re-run with LeakSanitizer enabled.

I ran the non-display binaries serially, including pure suites that merely
match the Makefile's display-name patterns. Per-suite logs and all original
statuses are in `build/edit-5ih-nondisplay/` and
`build/edit-5ih-nondisplay.log`. (M)[AC] status values derive from each child
process's exit result. The failed socket-dependent tests were checked
separately with local-socket permission, retaining this worktree as cwd. No
display was opened by those headless runs.

The final scoped font sanitizer run:

```sh
ASAN_OPTIONS=detect_leaks=0 build/san/tests/font_selection_test
```

```text
sublimite: invalid font selection; using embedded DejaVu Sans Mono
sublimite: font selection failed; using embedded DejaVu Sans Mono
sublimite: font selection failed; using embedded DejaVu Sans Mono
font_selection_test: family/file parsing, precedence inputs, TTC index and bounds passed
font_selection_test: proportional design advances rejected despite equal rounded pixel widths
font_selection_test: config startup/reload changes submitted glyph pixels, unchanged family skips rebuild, removal restores default, typing mallocs=0
font_selection_test: frame boundary, input sequences, worker identity, mallocs=0 guard=ASan-inert
font_selection_test: TTC face selection, geometry recomputation and single adoption passed
font_selection_test: DPI 96 selected Unicode pixels, zoom/reset, glyph retirement, runnable input and outstanding selection shutdown passed
font_selection_test: DPI 243 selected Unicode pixels, zoom/reset, glyph retirement, runnable input and outstanding selection shutdown passed
font_selection_test: environment, installed family, failure fallback, proportional rejection, file precedence and restore passed
```

`font_cli_test`, `config_test`, `config_editor_test`, `config_worker_test`,
`font_size_test`, `font_test` and `keys_test` also pass with ASan/UBSan. The
source-wide override and reload cases are exercised by the scoped font suite
in both release and sanitizer builds.

Additional headless verification succeeded:

```sh
ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_close_test --headless
ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 build/san/tests/editor_large_test
ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 build/san/tests/editor_test --headless
ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 build/san/tests/editor_p4_test --headless
ASAN_OPTIONS=detect_leaks=0 build/san/tests/gl_gate_test
ASAN_OPTIONS=detect_leaks=0 build/san/tests/x11_display_guard_test
```

The large/editor/P4 runs required permission for local IPC sockets. Tail
evidence from the headless editor suites follows; their workload counts are
inherited fixture requirements, not new budgets:

```text
editor_test: mutation error is negative and stops the loop passed
editor_test: null script and journal byte model passed
editor_test: null 10000 keys mallocs=0 guard=ASan-inert
editor_test: idle blink timeout and unfocused timer disarm passed
editor_test: queue depth one and coalesced edits passed
editor_test: headless passed
editor_p4_test: null interrupted index recovery/stale indicator passed (stale frames=3)
editor_p4_test: null 100 tabs 10000 mixed keys all modules mallocs=0 guard=ASan-inert
editor_p4_test: all passed
```

The large-editor suite passed after allowing local Unix sockets; its initial
sandbox failure was `ipc_server_init`, not a font or layout failure.

## Findings outside this bead, left unchanged

The full non-display check is **not entirely green**:

- `journal_kill_test` fails reproducibly in its initial replay with
  `JOURNAL_CALLBACK`. The original sweep and a later serial confirmation
  produced the same result. No journal source or test was modified.
- With local-socket permission, `ipc_test astra_many_open` and
  `ipc_test astra_many_open_local` accept all fixture files, then fail the
  existing Source changed assertion after retirement. Other non-display
  named IPC cases pass. Native primary/isolated launch and capacity cases
  remain for the coordinator's display environment. These many-open cases
  do not enable font preferences or request a font selection. No source-watch
  or retirement changes were made.

Exact failure evidence:

```text
journal_kill_test: FAIL trial=0 target=1 issued=1 page_cache=1 ack=0 replay=0 rc=6 error=0
astra_many_open: mode=cli requested=200 accepted=200 tabs=200
FAIL tests/ipc_test.c:918: editor_get_stats(e).source_stale
astra_many_open: mode=local requested=200 accepted=200 tabs=200
FAIL tests/ipc_test.c:918: editor_get_stats(e).source_stale
```

(P) failure source locations derive from `__LINE__`; journal return values
derive from `journal_error`. (M)[AC] fixture progress values derive from the
test's atomic progress counters. The many-open population is the inherited
`ASTRA_OPEN_FILES` acceptance fixture in `tests/ipc_test.c`, not a new count.
These failures need coordinator triage in their own scope; I did not fix them.

## Benchmark and what remains

The requested post-swap G1/G3 run succeeded with Go Mono. Its full output is
`build/edit-5ih-new-baseline.log` and is pasted as a NEW baseline in
`docs/decisions/edit-5ih.md`, with power/load stamps and derivations. It
reports zero typing allocations (M)[AC], from release interposition. Config
and IPC module benches also succeeded and are recorded there.

No font/config/CLI wiring remains in this bead's requested implementation.
Validation still requires the coordinator's leaks-enabled run, native display
suites on Xvfb :99, and resolution/triage of the unrelated journal and
source-watch test failures above. Full `make check` cannot be claimed green.
The requested implementation stays out of input.c and session.c.
