# edit-x8i worker report

Implemented configurable find/replace dispatch. `input.c` resolves the binding
before dispatching find actions; its early Escape path is unchanged. The panel
still owns field editing and cancellation. Default option bindings are Alt+R,
Alt+C and Alt+W, configurable as `toggle_regex`, `toggle_case_sensitive` and
`toggle_whole_word`.

Only executable find/open-replace/next/previous default rows lose the
unimplemented flag, including the dedicated Find key. The unavailable-action
regression no longer excludes physical find shortcuts. No allocation or new
capacity/time budget was added to dispatch.

Files touched:

- `src/config/actions.inc`
- `src/editor/findpanel.c`
- `src/editor/input.c`
- `src/editor/private.h`
- `src/keys/keys.c`
- `src/keys/keys.h`
- `tests/config_find_test.c` (new, null renderer)
- `tests/editor_test.c`
- `tests/keys_test.c`
- `docs/decisions/edit-x8i.md`
- `docs/worker-reports/edit-x8i-s6.md`

Build/test logs are local artifacts under `build/edit-x8i-*`.
No forbidden tracking commands were run, and no prohibited coordinator documents were edited.

## Evidence convention

All numerical output in the transcripts below is (M)[AC], copied directly from
commands and tests in this worktree; BAT0 was `Not charging` before validation.
Exit status is the observed command status. Compiler versions are observed tool
identifiers. Binding counts derive from the compiled table; lookup counts derive
from the existing test loops. Fixture constants in the new regression are (P),
with derivations beside the code. No new measured timing limit is claimed.

## Red, before implementation

Built the new regression with the existing implementation. Both panel states
still navigated on the removed default shortcut after installing
`find_next='Ctrl+J'`. Leak detection was disabled for the intentional early test
failure; ASan/UBSan remained enabled.

```text
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/config_find_test
config_find_test: FAIL editor_view(e).selection.cursor == before.cursor && editor_view(e).selection.anchor == before.anchor
config_find_test: FAIL find_next_override(true) == 0
exit: 1

$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/config_find_test --closed
config_find_test: FAIL editor_view(e).selection.cursor == before.cursor && editor_view(e).selection.anchor == before.anchor
exit: 1
```

## Green

```text
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/config_find_test
config_find_test: find-next override with panel open passed
config_find_test: find-next override with panel closed passed
config_find_test: open/replace/previous/toggle overrides and local editing passed
exit: 0

$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/keys_test
keys_test: 259 independent binding rows passed
keys_test: 20000 lookups mallocs=0 guard=ASan-inert
keys_test: all passed
exit: 0

$ make -j"$(nproc)" all
[gcc release compilation/link output: build/edit-x8i-all.log]
exit: 0

$ build/tests/config_find_test
config_find_test: find-next override with panel open passed
config_find_test: find-next override with panel closed passed
config_find_test: open/replace/previous/toggle overrides and local editing passed
exit: 0
```

The release regression guards each injected key through settlement with the
active counting allocator. Sanitizer mode reports its allocator guard as inert.
The regression checks old/new find-next shortcuts with the panel open and closed,
find/replace and previous-match overrides, all option overrides, lock/uppercase
normalization, local select-all/paste/Escape, and the unavailable-action counter.

Existing editor validation:

```text
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --73j-find
edit-73j find/replace, disjoint undo/redo and journal recovery passed
edit-73j replacement Escape/error prefixes, undo and recovery passed
edit-73j distant wrapped/clipped find results are visible passed
exit: 0

$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --headless
[full output: build/edit-x8i-editor-headless.log]
editor_test: headless passed
exit: 0
```

This includes the existing focused-panel editing regressions and all retained
unimplemented-action notices without the old shortcut exclusions.

## Full validation and remaining work

Full `make check` remains failing. The release build, bead regressions,
existing find/findui tests and headless editor suite pass. The remaining journal
failure and socket-dependent checks are outside this bead's implementation scope.

Toolchain observed: gcc 13.3.0 release and clang 18.1.3 ASan/UBSan (M)[AC], with
`-std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion`. Compilation succeeded for
all release and sanitizer test binaries.

```text
$ ASAN_OPTIONS=detect_leaks=0 make -j"$(nproc)" check
[compile/test output: build/edit-x8i-check.log]
cli_test:391: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
make: *** [Makefile:131: check] Error 1
exit: 2
```

A filtered `make check` attempt then stopped in `file_kill_test`; it requires a
real display through `bench/file_bench.c`'s `view_init`, despite being absent from
the Makefile's display-suite list. It failed again on an isolated rerun. The
remaining tests were executed sequentially so that the early failures did not
hide other results. No Makefile change was made.

Passing sanitizer suites, including isolated reruns where indicated below:

`base_test`, `config_editor_test`, `config_find_test`, `config_test`, `config_worker_test`, `editor_close_test`, `editor_hot_exit_test`, `editor_scroll_test`, `editor_session_test`, `file_test`, `find_bench_test`, `find_bestof_test`, `find_p1r_test`, `find_simd_filter_verify_test`, `find_test`, `find_timeout_test`, `findui_test`, `font_selection_test`, `font_size_test`, `font_test`, `gl_gate_test`, `harness_test`, `hl_parse_test`, `hl_test`, `hl_worker_test`, `indent_test`, `journal_test`, `keys_test`, `layout_test`, `lineidx_test`, `minimap_test`, `piece_bounds_test`, `piece_mem_test`, `piece_mt_test`, `piece_test`, `prewake_test`, `raster_scroll_test`, `render_test`, `replay_test`, `runtime_test`, `savectl_reload_test`, `savectl_test`, `scan_test`, `scroll_display_test`, `scroll_test`, `startup_test`, `tabs_test`, `trace_pairs_test`, `trace_test`, `undo_test`, `unicode_render_test`, `utf8_tables_test`, `utf8_test`, `view_test`, `wayland_bench_test`, `wayland_gl_test`, `wayland_test`, `window_size_test`, `work_foreground_test`, `work_test`, `wrap_test`, `x11_alignment_test`, `x11_display_guard_test`, `x11_input_test`, `x11_xi2_test`.

The full `editor_test --headless` also passed. The hot-exit, save/session,
scroll and headless close suites passed. `editor_large_test` passed its preceding
near-end navigation, cold paint, cancelled-find restart and related checks,
then stopped when its IPC fixture attempted to initialize a Unix server.

The non-display runner results are in `build/edit-x8i-nondisplay-results.json`;
additional pure/headless cases are in `build/edit-x8i-extra-results.json` and
`build/edit-x8i-editor-more-results.json`. Individual outputs are saved as
`build/edit-x8i-suite-<suite>.log` and `build/edit-x8i-extra-<suite>.log`.

Shell checks also passed:

```text
$ ASAN_OPTIONS=detect_leaks=0 sh tools/test_replay_cli.sh build
test_replay_cli: all passed
exit: 0

$ ASAN_OPTIONS=detect_leaks=0 sh tools/test_display_lock.sh contract build/san/tests/refwin_test
display lock contract: owner PID, alias/timeout, make/scripts, nested inheritance, exit status PASS
exit: 0
```

## Benchmarks

All numeric benchmark output below is (M)[AC], directly measured by the existing
benchmarks. Their sample/workload counts come from their existing source; no
count or timing budget was introduced for this bead. The editor row uses its
existing corpus, default key count and serial workload. Output times are in
nanoseconds unless explicitly labeled otherwise. These are tracked observations,
without a before/after performance comparison.

```text
$ build/bench/keys_bench
POWER status=Not charging [AC] load1=7.18; keys measurements TRACK only
BENCH keys_lookup TRACK n=1000000 p50=0.059 us p99=0.138 us (M)[AC] load1=7.18 no limit (no derivation for 1 us)
STREAM (M)[AC] load1=7.18 matches=436904 prefixes=250596 cancelled=125596 misses=186904 mallocs=0 guard=active gate_mallocs=0(G)
exit: 0

$ build/bench/config_bench
config_bench: parse (M)[AC] power=Not charging load1=2.65 n=77 derivation=one_sample_per_fixture_byte fixture_bytes=77 p50_ns=3657 p99_ns=27544 verdict=TRACK
exit: 0

$ DISPLAY=:99 EDIT_DISPLAY=:99 build/bench/editor_bench --track --no-idle --no-p4 --serial-only
STAMP (M)[AC] BAT0=Not charging load1=6.80 shared box
BENCH name=editor_null_serial_injection_T4_G1 n=10000 required_n=10000 p50=976424 p99=1096851 ci95_p50=[976320,976523] ci95_p99=[1049709,1758812] gate_p50=1000000 gate_p99=2000000 dropped=0 (M)[AC] power=Not charging load1=6.80 verdict=TRACK
G1 null workload=serial keys=10000 completed=10000 arrivals_active=0 coalesced=0 foreign_input=0 allocations=0 mutations=10000 journal=10000 structural=OK mode=TRACK (M)[AC]
STAMP (M)[AC] BAT0=Not charging load1=6.06 shared box
editor_bench:555 failed: editor_open(&e, &cfg, &b) == 0
exit: 1
```

The editor null-renderer row completed with its structural checks passing and
no allocations. The subsequent native-renderer row could not open without the
X socket. Full output is `build/edit-x8i-editor-bench.log`.

## Remaining validation and findings outside the bead

- `journal_kill_test` failed in the sequential sweep and on an isolated rerun:
  the initial kill-before-pump case reports a replay error. No journal code or
  journal test was changed. This prevents a fully green non-display gate.

```text
journal_kill_test: FAIL trial=0 target=1 issued=1 page_cache=1 ack=0 replay=0 rc=6 error=0
exit: 1
```

- `ipc_test` cannot initialize its Unix server in this sandbox. The CLI and
  large-editor startup fixtures stop at the same kind of server initialization.
  Native/socket-enabled validation remains for the coordinator.

```text
init rc=4 errno=1 (Operation not permitted)
FAIL tests/ipc_test.c:130: init_rc==IPC_OK
exit: 1

editor_large_test:215: FAIL ipc_server_init(&server, runtime) == IPC_OK
editor_large_test:1005: FAIL mapped_startup_ipc() == 0
exit: 1
```

- Display suites cannot run inside this sandbox for lack of the X socket.
  The coordinator must rerun them on Xvfb :99, including the full editor,
  file visibility, native clipboard/renderer and CLI paths. A direct native
  probe confirms the platform initialization failure:

```text
$ ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 build/san/tests/x11_identity_test
x11_identity_test:25: FAIL plat_init(&p, &cfg) == PLAT_OK
exit: 1
```

- Default LeakSanitizer execution aborted with its ptrace restriction diagnostic.
  Sanitizer runs therefore used `ASAN_OPTIONS=detect_leaks=0`; ASan and UBSan stayed
  enabled. Leak detection remains for validation outside the sandbox.
- `file_test` initially failed its shared-watch notification assertion at
  `tests/file_test.c:308`; its isolated rerun passed. `lineidx_test` initially
  failed the file-backed mmap readiness assertion at `tests/lineidx_test.c:339`;
  its isolated rerun also passed. Initial and rerun logs are retained. Neither
  file watching nor index code was changed; the cause of those intermittent
  failures was not established.
- The Makefile's display classification omits `file_kill_test` despite its native
  viewport fixture. This is reported for coordination, not repaired here.

No implementation remains for edit-x8i. The coordinator still needs to resolve
or triage the unrelated journal failure and perform the socket/display/leak
validation above. This worker stops after this bead; no tracking-state changes
or further fixes were made.
