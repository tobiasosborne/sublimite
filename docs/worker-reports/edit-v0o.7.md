# edit-v0o.7 — config and themes worker report

Implemented the config module and editor application path in this worktree.
GCC release builds and the config suites are green. Full acceptance remains
subject to the sandbox exceptions, existing suite failures, and dependency
integration recorded below. No `bd` or `git` was run. No other worktree was used.

## Files and behavior

- `src/config/config.h`, `parser.c`, `settings.c`, `runtime.c`, `theme.c`,
  `actions.inc`, `private.h`: bounded TOML visitor, transactional typed settings, XDG/HOME
  path, placeholder palettes, named action overrides, worker reads and mailbox
  publication. The worker creates the config parent for initially absent files.
  Invalid edits keep the previous snapshot; deletion restores defaults.
- `src/file/file.h`, `file.c`: caller-owned path watches share the existing
  process service and parent watches with editor files; closing a member keeps
  peers alive. Dispatch stays bounded and allocation-free.
- `src/keys/keys.h`, `keys.c`: overrides and defaults share stroke normalization
  and chord handling; overriding an action removes its default bindings.
- `src/view/view.h`, `view.c`: idle tab-width setter invalidates visual-column
  seeds and preferred column.
- `src/editor/editor.h`, `private.h`, `preferences.c`, `open.c`, `buffers.c`,
  `input.c`, `editor.c`, `paint.c`, and `src/main.c`: native config startup,
  eventfd/shared-watch polling, frame application, per-buffer defaults, keymap
  routing and palette use in text/chrome/minimap/decorations. Font points/family
  are exposed through `editor_preferences` for the landing font/DPI consumer.
- `tests/config_test.c`, `tests/config_editor_test.c`, `tests/config_worker_test.c`, `fuzz/config_fuzz.c`,
  `bench/config_bench.c`: parser, reload, frame, allocation, fuzz and tracked
  benchmark coverage.
- `docs/config.md`, `docs/decisions/edit-v0o.7.md`, `src/config/STATUS.md`, this
  report, and appended worklog/handoff entries: contract, usage and evidence.

Bounds and numeric fixture inputs have their derivations in the header/tests.
Parser mutation coverage is every fixture byte position times every octet
(`sizeof seed - 1` multiplied by `UINT8_MAX + 1`), not an invented time budget.
The frame test implements the bead's one-generation requirement (G): an open
frame is reused, otherwise `render_frame_begin` creates its immediate successor.
It observes mailbox publication at the I/O boundary and the first containing
submit. It does not turn unbounded filesystem/OS scheduling into a wall-time
refresh guarantee.

## Red run

The test and public header were written before implementation. This frozen-API
red run failed at link time because the module did not exist:

```sh
make build/san/tests/config_test
```

```text
/usr/bin/ld: build/san/tests/config_test.o: in function `main':
/home/tobias/Projects/sublimite/.wt/edit-v0o.7/tests/config_test.c:14:(.text+0x2d5): undefined reference to `config_defaults'
/usr/bin/ld: /home/tobias/Projects/sublimite/.wt/edit-v0o.7/tests/config_test.c:15:(.text+0x2f3): undefined reference to `config_parse'
/usr/bin/ld: /home/tobias/Projects/sublimite/.wt/edit-v0o.7/tests/config_test.c:20:(.text+0x3c9): undefined reference to `config_keys_lookup'
/usr/bin/ld: /home/tobias/Projects/sublimite/.wt/edit-v0o.7/tests/config_test.c:24:(.text+0x488): undefined reference to `config_parse'
clang: error: linker command failed with exit code 1 (use -v to see invocation)
make: *** [Makefile:97: build/san/tests/config_test] Error 1
```

Diagnostic numbers above are compiler source locations/offsets and shell exit
status, reproduced literally, rather than measurements or planning bounds.

## Green runs

The sandbox rejects LeakSanitizer's thread inspection. These runs retain
AddressSanitizer and UndefinedBehaviorSanitizer and disable only leak scanning:

```sh
ASAN_OPTIONS=detect_leaks=0 build/san/tests/config_test
ASAN_OPTIONS=detect_leaks=0 build/san/tests/config_editor_test
```

```text
config_test: TOML types, malformed input and exhaustive single-byte mutations passed
config_test: absent file, inotify create/rename/delete, invalid reload and shared watch lifetime passed
config_test: parser, settings and keymap passed
config_editor_test: next frame generation, theme, wrap, tabs, font contract and keymap without typing allocations passed
```

Release runs enable the real allocation guard (ASan's allocator interposition
makes that guard inert in sanitizer builds):

```sh
build/tests/config_test
build/tests/config_editor_test
```

```text
config_test: TOML types, malformed input and exhaustive single-byte mutations passed
config_test: absent file, inotify create/rename/delete, invalid reload and shared watch lifetime passed
config_test: parser, settings and keymap passed
config_editor_test: next frame generation, theme, wrap, tabs, font contract and keymap without typing allocations passed
```

The parser suite covers supported types, malformed text, decimal extrema,
Unicode/UTF-8 rejection, duplicate definitions, source/key/value/entry bounds,
transactionality and exhaustive single-byte mutations. The runtime suite covers
initial absence, create/rename/delete, invalid reload preservation and shared
watch retirement. The headless editor suite covers the first containing frame,
palettes, wrapping, tab stops/spaces, removal of overrides, tab switching,
font setting publication, shortcut execution, and no typing allocations.

## Build and benchmark evidence

`make all` passes with GCC and the required C/warning flags. Its final run says:

```text
make: Nothing to be done for 'all'.
```

Earlier release iterations caught an unnecessary transitive file typedef in the
config header, an unchecked benchmark input return, and an uninitialized
headless backend fixture. All were corrected within this bead. The config
header no longer exposes the file implementation's type namespace.

The parser benchmark checks power/load and reports tracked percentiles. Its
sample count derives from the byte length of its representative settings
fixture; the values are clock-end minus clock-start observations (M)[AC].
There is no invented config parsing gate:

```text
config_bench: parse (M)[AC] power=Charging load1=18.09 n=77 derivation=one_sample_per_fixture_byte fixture_bytes=77 p50_ns=9953 p99_ns=199811 verdict=TRACK
```

Related module benches were also attempted: keys and view print tracked rows;
file cannot initialize its required display renderer; editor completes null
backend typing rows, then fails display backend open. Their complete logs are
archived under `build/config-verification/`. No perf miss was used as a blocker
or repaired outside this bead.

The config libFuzzer target builds with the same sanitizers. Corpus-only
execution (`-runs=0`, the fuzzer's no-mutation mode) executes the documented
valid/malformed seed corpus; the unit suite supplies exhaustive byte mutation
coverage. This is not a sustained fuzz campaign. Exact output is archived.

The file mutable-global checker passes: only the existing signal service and
inotify service are mutable. Path members add no new process global.

## Additional behavioral red/green

A dotted assignment beneath a table previously declared by a header was being
accepted when the parent table was later opened. The new invalid fixture is
`[a.b]` followed by a leaf, then `[a]` followed by `b.c`. The fixture's leaf
integers are syntax inputs only. The regression was added and run before the
parser fix:

```sh
make build/tests/config_test
build/tests/config_test
```

```text
config_test: FAIL config_parse((const uint8_t *)bad[i], strlen(bad[i]), &settings, &error) != CONFIG_OK
config_test: FAIL parser_test() == 0
```

After the parser rejects redefinition of the existing table, the same run is
green (and the sanitizer config suites were rerun too):

```text
config_test: TOML types, malformed input and exhaustive single-byte mutations passed
config_test: absent file, inotify create/rename/delete, invalid reload and shared watch lifetime passed
config_test: parser, settings and keymap passed
```

## Worker cancellation red/green

The worker parser also polls cancellation while validating and scanning
untrusted text, including long comments/whitespace. Its byte polling boundary
is the existing `VIEW_SCAN_BOUND` (P), not an invented duration. Public parsing
uses the same implementation without worker control. A stale-epoch test checks
transactionality, and cancellation from the first callback checks that a
subsequent assignment is not visited after an inherited byte-work gap.

The test/private declarations preceded implementation:

```sh
make build/tests/config_worker_test
```

```text
gcc -O2 -g -msse2 -pthread build/rel/tests/config_worker_test.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/tests/config_worker_test
/usr/bin/ld: build/rel/tests/config_worker_test.o: in function `main':
/home/tobias/Projects/sublimite/.wt/edit-v0o.7/tests/config_worker_test.c:19:(.text.startup+0x19a): undefined reference to `config_parse_worker'
/usr/bin/ld: /home/tobias/Projects/sublimite/.wt/edit-v0o.7/tests/config_worker_test.c:27:(.text.startup+0x262): undefined reference to `config_toml_parse_worker'
collect2: error: ld returned 1 exit status
make: *** [Makefile:91: build/tests/config_worker_test] Error 1
```

Release and ASan/UBSan runs are green:

```sh
build/tests/config_worker_test
ASAN_OPTIONS=detect_leaks=0 build/san/tests/config_worker_test
```

```text
config_worker_test: cancelled parse preserves settings and polls inside untrusted text passed
```

## Full-suite outcomes and sandbox limits

`make check` was run as requested. The plain run stops at `base_test` because
LeakSanitizer cannot inspect sandboxed threads:

```text
gcc -O2 -g -msse2 -pthread build/rel/tools/replay.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/tools/replay
== build/san/tests/base_test
==618==LeakSanitizer has encountered a fatal error.
==618==HINT: For debugging, try setting environment variable LSAN_OPTIONS=verbosity=1:log_threads=1
==618==HINT: LeakSanitizer does not work under ptrace (strace, gdb, etc)
make: *** [Makefile:105: check] Error 1
```

With only leak scanning disabled, make check still fails. An initial run took
the normal shared display lock and reached CLI's denied IPC setup. Later runs
used `EDIT_DISPLAY_LOCK_TIMEOUT=0`: zero means immediate refusal of a contended
lock (P), avoiding a wait for a display this sandbox cannot access. The final
check attempt reaches:

```text
base_test: PASS
== build/san/tests/cli_test
cli_test:215: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
cli_test: §36 forced startup timeout correctly failed
cli_test:215: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
make: *** [Makefile:105: check] Error 1
```

Because make check stops at its first failure, every sanitizer test binary was
also attempted individually, with a private runtime directory and the same
ASan/UBSan configuration. The unavailable display lock was not allowed to
strand the remaining independent suites. The result manifest has no missing
suite relative to the test-binary glob. Non-config suites were not rerun just
because an isolated config parser check changed; the config suites and fuzzer
were rerun after every final parser change.

The following exit statuses are process observations (M)[AC]; zero is success and
nonzero is failure by the suites' Unix exit convention (P). They are not
performance bounds. Native GL diagnostics explicitly skipped unavailable
context acquisition despite successful pure checks.

| Suite | Observed exit status |
| --- | --- |
| `base_test` | `0` |
| `cli_test` | `1` |
| `config_editor_test` | `0` |
| `config_test` | `0` |
| `config_worker_test` | `0` |
| `editor_close_test` | `1` |
| `editor_large_test` | `1` |
| `editor_p4_test` | `1` |
| `editor_test` | `1` |
| `file_kill_test` | `1` |
| `file_test` | `0` |
| `find_bench_test` | `0` |
| `find_bestof_test` | `0` |
| `find_p1r_test` | `0` |
| `find_simd_filter_verify_test` | `0` |
| `find_test` | `0` |
| `find_timeout_test` | `0` |
| `findui_test` | `0` |
| `font_test` | `0` |
| `gl_gate_test` | `0` |
| `gl_test` | `0` |
| `harness_test` | `0` |
| `indent_test` | `0` |
| `ipc_test` | `1` |
| `journal_kill_test` | `0` |
| `journal_test` | `0` |
| `keys_test` | `0` |
| `layout_test` | `0` |
| `lineidx_test` | `0` |
| `minimap_test` | `0` |
| `piece_bounds_test` | `0` |
| `piece_mem_test` | `0` |
| `piece_mt_test` | `0` |
| `piece_test` | `0` |
| `prewake_test` | `0` |
| `raster_test` | `1` |
| `refproto_test` | `1` |
| `refwin_test` | `1` |
| `refwin_timeout_test` | `1` |
| `render_test` | `0` |
| `replay_test` | `0` |
| `runtime_test` | `0` |
| `savectl_reload_test` | `0` |
| `savectl_test` | `0` |
| `scan_test` | `0` |
| `scroll_test` | `0` |
| `tabs_test` | `0` |
| `trace_test` | `0` |
| `undo_test` | `0` |
| `unicode_render_test` | `0` |
| `utf8_tables_test` | `0` |
| `utf8_test` | `0` |
| `view_test` | `0` |
| `work_foreground_test` | `0` |
| `work_test` | `0` |
| `wrap_test` | `0` |
| `x11_alignment_test` | `0` |
| `x11_clip_review_test` | `1` |
| `x11_clip_test` | `1` |
| `x11_display_guard_test` | `0` |
| `x11_identity_test` | `1` |
| `x11_input_test` | `0` |
| `x11_live_test` | `1` |
| `x11_order_test` | `1` |
| `x11_stall_test` | `1` |
| `x11_xi2_test` | `0` |

Display-dependent failures: `editor_close_test`, `file_kill_test` (it opens a
viewport renderer), `raster_test`, `refproto_test`, `refwin_test`,
`refwin_timeout_test`, `x11_clip_review_test`, `x11_clip_test`,
`x11_identity_test`, `x11_live_test`, `x11_order_test`, `x11_stall_test`.
Their logs show failed platform/renderer initialization or connection errors.
`gl_test` passes pure tests while explicitly skipping native diagnostics.
The display-lock shell contract cannot acquire the shared display in this
sandbox. The coordinator must rerun these on the supplied Xvfb display :99
outside the sandbox, with normal leak checks.

Unix-socket denial also blocks `cli_test`, `ipc_test`, `editor_p4_test` and
`editor_large_test`'s IPC case. The direct IPC diagnostic is:

```text
init rc=4 errno=1 (Operation not permitted)
FAIL tests/ipc_test.c:130: init_rc==IPC_OK
```

These are not config regressions repaired by this bead. The replay CLI shell
contract, which does not need a display/socket server, passes. The failed
fixture locations below are literal compiler/test source locations.

## Findings outside scope; not fixed

`editor_test` fails its existing minimap probe with preferences disabled:

```text
editor_test:137: FAIL probe.frames >= keys && probe.stale_frames > 0
editor_test:1997: FAIL minimap_typing_flash() == 0
edit-g11 null typing: keys=95 frames=96 orange_frames=0 stale_frames=0 changed_pending=0 chrome_markers=0 mallocs=0 fill_max_ns=19470
```

The assertion requires a stale minimap frame; the recorded run has none and no
orange frame. This report does not establish a pre-change baseline (git use was
prohibited) or revise that other bead's test. It is a remaining headless suite
failure for coordinator triage; full make check is therefore not green here.

The first `editor_large_test` attempt failed its existing cooperative-work
wall deadline under shared load. A serial rerun passes that headless case and
continues through its other headless checks, then stops at denied IPC startup:

```text
editor_large_test:210: FAIL ipc_server_init(&server, runtime) == IPC_OK
editor_large_test:800: FAIL mapped_startup_ipc() == 0
P4-3 section 28: sealed find publication yields behind a saturated mailbox passed
P4-3 section 28: production find/warm yield bulk lane before completion passed
P4-3 section 21: warm completion requests an idle correcting frame passed
P4-3 section 20: cold deep bracket paint minor faults=2 (M)[AC], loose bound=128 (G)
P4-3 section 18: long-line start and UTF-8/combining/CRLF byte boundaries passed
P4-3 section 17: COPY coordinates and misleading mapped prefix resolve exactly passed
P4-3 section 15: Keep restarts incomplete index and cancelled warm lease passed
P4-3 section 1: held cancelled find survives Keep and restart passed
```

No editor, large-file, IPC, renderer, minimap, or unrelated test failure was
fixed outside config scope. Related benchmark misses are tracked observations,
not new budgets or grounds for another bead. Exact logs, manifest, compiler
build logs and fuzzer output are in `build/config-verification/` in this
worktree. `make all` ends green with the tree building.

## What remains

- The requested `docs/design/visual-spec.md` is absent here. Both palettes have
  provisional names and values; reconcile names against that dependency and
  obtain Tobias's palette approval through the coordinator.
- The font/DPI implementation landing in another bead must consume
  `font_points` and `font_family`. This worktree exposes and hot-reloads those
  settings; it does not convert points to pixels or rebuild/select the native
  font. Native font-size/family behavior is consequently not verified here.
- Coordinator verification outside the sandbox: display/socket/LSan suites,
  the existing stale-minimap assertion, and the next Astra comparison drive
  on the merged build. No hand-over to Tobias or full acceptance is claimed.

## Final boundary regressions

The worker cancellation check was extended to empty input. The added test was
red before the entry cancellation check:

```text
config_worker_test: FAIL config_parse_worker(NULL, 0, &settings, &error, &ctx) == CONFIG_ERR_BUSY
```

A maximum decoded string must use the text buffer's last byte for its NUL.
The test at `CONFIG_TEXT_CAP - 1` data bytes (P: capacity minus the terminator)
was red before correcting that boundary:

```text
config_test: FAIL config_parse((const uint8_t *)oversized, strlen(oversized), &settings, &error) == CONFIG_OK
config_test: FAIL parser_test() == 0
```

Final release and sanitizer green outputs, after both fixes:

```text
config_test: TOML types, malformed input and exhaustive single-byte mutations passed
config_test: absent file, inotify create/rename/delete, invalid reload and shared watch lifetime passed
config_test: parser, settings and keymap passed
config_editor_test: next frame generation, theme, wrap, tabs, font contract and keymap without typing allocations passed
config_worker_test: cancelled parse preserves settings and polls inside untrusted text passed
config_test: TOML types, malformed input and exhaustive single-byte mutations passed
config_test: absent file, inotify create/rename/delete, invalid reload and shared watch lifetime passed
config_test: parser, settings and keymap passed
config_editor_test: next frame generation, theme, wrap, tabs, font contract and keymap without typing allocations passed
config_worker_test: cancelled parse preserves settings and polls inside untrusted text passed
```

The final GCC `make all` succeeds. The final fuzzer corpus run succeeds; its
numeric fields are literal libFuzzer observations on AC, with corpus counts
and byte totals derived from the seed files and coverage from instrumentation:

```text
INFO: seed corpus: files: 6 min: 10b max: 78b total: 218b rss: 34Mb
#7	INITED cov: 546 ft: 846 corp: 6/218b exec/s: 0 rss: 36Mb
#7	DONE   cov: 546 ft: 846 corp: 6/218b lim: 78 exec/s: 0 rss: 36Mb
Done 7 runs in 0 second(s)
```

Final parser changes were followed by the scope tests, fuzzer, GCC build and
another full make check attempt. Independent suites had already all been
attempted; their exceptions above remain. Stopping after this bead.
