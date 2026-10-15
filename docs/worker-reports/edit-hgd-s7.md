# edit-hgd worker report

Work is confined to this worktree and this bead. No `bd` or `git` commands
were run. The protected coordination documents are unchanged.

## Evidence convention

All run outputs below are (M)[AC], with the measured BAT0 status shown.
Exit codes and source-line locations are observed process/assertion results.
Hexadecimal colours are (P) existing theme/layout palette data printed by the
test, not invented performance values. Fixture counts and offsets derive from
the literal strings; allocation expectations derive from CLAUDE's allocation
law. Compiler version numbers identify the installed toolchains.

## Red run, before implementation

Added the config interface declarations and regressions, then built them with
`make -j4 build/tests/editor_test build/tests/config_test` using the existing
strict release flags. This worktree initially had no build directory, so no
stale objects were reused. The composed-cell oracle reuses the indexing from
the existing `editor_p4_test` decorations test, with live chrome/gutter offsets.

```text
STAMP (M)[AC] BAT0=Charging; outputs and exit codes measured by subprocess
$ build/tests/editor_test --trailing-default
editor_test:116: FAIL plain
trailing typing default: space bg=0x493038 plain=0x20242c marker=0x493038
exit=1 (M)[AC]
$ build/tests/editor_test --trailing-all
editor_test:116: FAIL plain
editor_test:141: FAIL result == 0
trailing typing all: space bg=0x493038 plain=0x20242c marker=0x493038
exit=1 (M)[AC]
$ build/tests/editor_test --trailing-selection
editor_test:151: FAIL !(trailing_cell(e, 0, sizeof "word" - 1u)->attrs & RENDER_ATTR_UNDERLINE)
exit=1 (M)[AC]
$ build/tests/editor_test --trailing-disabled-scan
editor_test:61: FAIL rc == EDITOR_OK || rc == EDITOR_MORE
editor_test:182: FAIL settle(e) == 0
exit=1 (M)[AC]
$ build/tests/config_test
config_test: FAIL settings.trailing == cases[i].mode && settings.trailing_warning == cases[i].warning
config_test: FAIL trailing_test() == 0
exit=1 (M)[AC]
```

## Files touched and behavior

- `src/config/config.h`: trailing mode and fallback warning in the existing settings snapshot; parser entry capacity derives from the named scalar keys.
- `src/config/settings.c`: parses dotted/table `whitespace.trailing`; absent key defaults to `none`; invalid spelling/type accepts `none` with a warning.
- `src/config/runtime.c`: publishes the accepted fallback and exposes its warning through `config_runtime_error`, including after an enabled configuration.
- `src/editor/paint.c`: skips trailing-range computation for `none`; filters ranges for selection; suppresses the run ending at the caret and the byte immediately before a caret within a run. Existing theme keys and arena storage remain in use.
- `tests/editor_test.c`: typed default/all composed-background regressions, completed-line markers, forward/reverse/empty selection, and a disabled-scan sentinel. Typing uses the existing counting-allocator callbacks.
- `tests/config_test.c`: default, supported values, invalid spelling/type, live invalid fallback, warning clearing and retention of other valid settings.
- `tests/editor_p4_test.c`: existing decoration fixtures explicitly opt into `all` so their bracket/tab coverage remains meaningful under the new default.
- `docs/decisions/edit-hgd.md`: coordinator decisions and implementation choices.
- `docs/worker-reports/edit-hgd-s7.md`: this report.

## Green targeted release run

The final behavior was tested after the recorded red run. The output below
is copied from `build/edit-hgd-s7/green-targeted.log`.

```text
STAMP (M)[AC] BAT0=Charging; outputs and exit codes measured by subprocess
$ build/tests/editor_test --trailing-default
trailing typing default: space bg=0x20242c plain=0x20242c marker=0x493038
exit=0 (M)[AC]
$ build/tests/editor_test --trailing-all
trailing typing all: space bg=0x20242c plain=0x20242c marker=0x493038
trailing all: completed line marked, typing cell plain passed
exit=0 (M)[AC]
$ build/tests/editor_test --trailing-selection
trailing selection: empty, partial, reverse and cleared selection passed
exit=0 (M)[AC]
$ build/tests/editor_test --trailing-disabled-scan
trailing none: range computation bypassed passed
exit=0 (M)[AC]
$ build/tests/config_test
config_test: trailing whitespace defaults, values and invalid fallback passed
config_test: TOML types, malformed input and exhaustive single-byte mutations passed
config_test: absent file, inotify create/rename/delete, invalid reload and shared watch lifetime passed
config_test: parser, settings and keymap passed
exit=0 (M)[AC]
```

## Release build and allocation guard

The installed compilers are GCC `13.3.0` and Clang `18.1.3` (M)[AC], from
`--version`. Builds use the Makefile's C11, Wall/Wextra/Werror/Wshadow/Wconversion
flags, release GCC and Clang ASan/UBSan. A final freshness check after the
comment/interface documentation edits reported:

```text
STAMP (M)[AC] BAT0=Charging; outputs and exit codes measured by subprocess
$ make -j4 all
make: Nothing to be done for 'all'.
exit=0 (M)[AC]
```

The complete release `build/tests/editor_test --headless` passed. Its final
output includes the ordinary and highlighted typing allocation guards:

```text
editor_test: GPU completion progresses and idle disarms polling passed
editor_test: scrolled undo/redo viewport repair passed
editor_test: mutation error is negative and stops the loop passed
editor_test: null script and journal byte model passed
editor_test: null 10000 keys mallocs=0 guard=active
editor_test: null hl-on 10000 keys mallocs=0 guard=active
editor_test: idle blink timeout and unfocused timer disarm passed
editor_test: queue depth one and coalesced edits passed
editor_test: headless passed
exit=0 (M)[AC]
```

The separate release allocator command ran with `EDITOR_ALLOC_ONLY=null`:

```text
STAMP (M)[AC] BAT0=Charging; outputs and exit codes measured by subprocess
$ build/tests/editor_test
editor_test: null 10000 keys mallocs=0 guard=active
exit=0 (M)[AC]
```

The reported typing count is inherited from the existing allocation fixture;
zero allocations is (P) required by the allocation law. Sanitizer builds mark
this interposition inert, so release is the allocation evidence.

## Sandbox and display limitations

Plain `make -j4 check` built the sanitizer suites, then failed because the
sandbox prevents LeakSanitizer's process inspection, including in `base_test`:

```text
== build/san/tests/base_test
==2648==LeakSanitizer has encountered a fatal error.
==2648==HINT: For debugging, try setting environment variable LSAN_OPTIONS=verbosity=1:log_threads=1
==2648==HINT: LeakSanitizer does not work under ptrace (strace, gdb, etc)
make: *** [Makefile:206: check] Error 1
exit=2 (M)[AC]
```

The sandbox also rejects a local AF_UNIX bind with `EPERM`, which prevented
the headless P4 IPC setup. Its decoration-only null-backend run passed. A
prepared runner limited to non-display tests was approved for execution outside
the sandbox, preserving default leak detection as well as ASan/UBSan. Native
display suites were excluded from that runner.

A direct native display probe inside the sandbox confirms the X socket is
inaccessible. Leak detection was disabled only for this connection probe:

```text
STAMP (M)[AC] BAT0=Charging; LeakSanitizer disabled only to isolate display access failure
$ DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/san/tests/x11_live_test
FAIL tests/x11_live_test.c:118: §22 colormap protocol error returned -1
FAIL tests/x11_live_test.c:118: §22 window protocol error returned -1
FAIL tests/x11_live_test.c:118: §22 present protocol error returned -1
x11_live_test: private display connection failed
§22 startup errors: FAILED
exit=1 (M)[AC]
```

The coordinator must rerun the native display suites on Xvfb `:99` (P:
the designated test display identity). No native display pass is claimed here.

## Tracked benchmarks

Config parsing used its unchanged fixture and sample-count derivation:

```text
STAMP (M)[AC] BAT0=Charging; outputs and exit codes measured by subprocess
$ build/bench/config_bench
config_bench: parse (M)[AC] power=Charging load1=7.73 n=77 derivation=one_sample_per_fixture_byte fixture_bytes=77 p50_ns=4469 p99_ns=35573 verdict=TRACK
exit=0 (M)[AC]
```

The standard large-file corpus was absent. The editor serial row therefore
used a private copy of `tests/editor_test.c`; this is a local fixture result,
not a comparison with the large-file baseline. The count and tracked goal
columns are inherited from the existing harness. Timings derive from monotonic
endpoint subtraction and the harness's percentile calculations; power/load,
mutation/journal counts and allocations are measured by that harness. The null
row completed; the following native backend could not initialize in the sandbox:

```text
STAMP (M)[AC] BAT0=Charging; fixture is a copy of tests/editor_test.c, bytes=290312 derived from stat
$ env -u EDIT_BACKEND build/bench/editor_bench --track --serial-only --no-idle --no-p4 --file=build/edit-hgd-s7/editor-bench-input.c
STAMP (M)[AC] BAT0=Charging load1=6.07 shared box
BACKEND requested=null actual=null init_error=0 TARGET A=2880x1800 font_px=30 (M)[AC]
POSITION null byte=261280 index=published workload=serial edits=alternating_insert_backspace
BENCH name=editor_null_serial_injection_submit n=10000 required_n=10000 p50=485672 p99=942057 ci95_p50=[485303,486044] ci95_p99=[922470,1018050] gate_p50=0 gate_p99=0 dropped=0 (M)[AC] power=Charging load1=6.07 verdict=TRACK
BENCH name=editor_null_serial_dequeue_submit n=10000 required_n=10000 p50=464456 p99=889661 ci95_p50=[464114,464767] ci95_p99=[874783,942437] gate_p50=0 gate_p99=0 dropped=0 (M)[AC] power=Charging load1=6.07 verdict=TRACK
BENCH name=editor_null_serial_injection_T4_G1 n=10000 required_n=10000 p50=645569 p99=1232418 ci95_p50=[645160,645963] ci95_p99=[1218360,1291152] gate_p50=1000000 gate_p99=2000000 dropped=0 (M)[AC] power=Charging load1=6.07 verdict=TRACK
G1 null workload=serial keys=10000 completed=10000 arrivals_active=0 coalesced=0 foreign_input=0 allocations=0 mutations=10000 journal=10000 structural=OK mode=TRACK (M)[AC]
STAMP (M)[AC] BAT0=Charging load1=6.06 shared box
editor_bench:555 failed: editor_open(&e, &cfg, &b) == 0
exit=1 (M)[AC]
```

After the missing standard corpus was generated for the required test
retries, the same inherited serial benchmark was also run against its default
large-file fixture. No key count or timing goal was changed. This is a tracked
shared-box result; its inherited typing goal is missed at the upper percentile,
not evidence of a regression against a paired baseline. The native backend
again could not initialize in the sandbox:

```text
STAMP (M)[AC] BAT0=Full; standard fixture generated from tools/mkcorpus.c
$ env -u EDIT_BACKEND build/bench/editor_bench --track --serial-only --no-idle --no-p4
STAMP (M)[AC] BAT0=Full load1=3.73 shared box
BACKEND requested=null actual=null init_error=0 TARGET A=2880x1800 font_px=30 (M)[AC]
POSITION null byte=966367641 index=progressing workload=serial edits=alternating_insert_backspace
BENCH name=editor_null_serial_injection_submit n=10000 required_n=10000 p50=1579966 p99=2240887 ci95_p50=[1572752,1600058] ci95_p99=[2207503,2319466] gate_p50=0 gate_p99=0 dropped=0 (M)[AC] power=Full load1=3.73 verdict=TRACK
BENCH name=editor_null_serial_dequeue_submit n=10000 required_n=10000 p50=1528141 p99=2150504 ci95_p50=[1521703,1547891] ci95_p99=[2123940,2224153] gate_p50=0 gate_p99=0 dropped=0 (M)[AC] power=Full load1=3.73 verdict=TRACK
BENCH name=editor_null_serial_injection_T4_G1 n=10000 required_n=10000 p50=1841648 p99=2586017 ci95_p50=[1833124,1868920] ci95_p99=[2556145,2651362] gate_p50=1000000 gate_p99=2000000 dropped=0 (M)[AC] power=Full load1=3.73 verdict=TRACK
G1 null workload=serial keys=10000 completed=10000 arrivals_active=0 coalesced=0 foreign_input=0 allocations=0 mutations=10000 journal=10000 structural=OK mode=TRACK (M)[AC]
STAMP (M)[AC] BAT0=Full load1=4.33 shared box
editor_bench:555 failed: editor_open(&e, &cfg, &b) == 0
exit=1 (M)[AC]
```

## Final non-display sanitizer results

The approved runner is `build/edit-hgd-s7/verify_non_display.py`. It uses the
sanitizer binaries built by `make check`, with default leak detection enabled.
The Makefile's display-lane filter is supplemented by its null-backend editor
variants and explicitly mocked/non-display GL and display-guard tests. The
headless release P4 suite and replay CLI shell contract also ran.

Final combined result: all 80 commands passed (M)[AC], derived by
enumerating the runner commands and replacing only the failed-prerequisite
results with their successful retries. The initial and retry logs are retained.

```text
base_test: exit=0 (M)[AC] BAT0=Charging log=build/edit-hgd-s7/verified-base_test.log
config_editor_test: exit=0 (M)[AC] BAT0=Charging log=build/edit-hgd-s7/verified-config_editor_test.log
config_find_test: exit=0 (M)[AC] BAT0=Charging log=build/edit-hgd-s7/verified-config_find_test.log
config_test: exit=0 (M)[AC] BAT0=Charging log=build/edit-hgd-s7/verified-config_test.log
config_worker_test: exit=0 (M)[AC] BAT0=Charging log=build/edit-hgd-s7/verified-config_worker_test.log
dataloss_test: exit=0 (M)[AC] BAT0=Charging log=build/edit-hgd-s7/verified-dataloss_test.log
file_kill_test: exit=0 (M)[AC] BAT0=Charging log=build/edit-hgd-s7/verified-file_kill_test.log
file_test: exit=0 (M)[AC] BAT0=Charging log=build/edit-hgd-s7/verified-file_test.log
find_bench_test: exit=0 (M)[AC] BAT0=Charging log=build/edit-hgd-s7/verified-find_bench_test.log
find_bestof_test: exit=0 (M)[AC] BAT0=Charging log=build/edit-hgd-s7/verified-find_bestof_test.log
find_p1r_test: exit=0 (M)[AC] BAT0=Charging log=build/edit-hgd-s7/verified-find_p1r_test.log
find_simd_filter_verify_test: exit=0 (M)[AC] BAT0=Charging log=build/edit-hgd-s7/verified-find_simd_filter_verify_test.log
find_test: exit=0 (M)[AC] BAT0=Charging log=build/edit-hgd-s7/verified-find_test.log
find_timeout_test: exit=0 (M)[AC] BAT0=Charging log=build/edit-hgd-s7/verified-find_timeout_test.log
findui_test: exit=0 (M)[AC] BAT0=Charging log=build/edit-hgd-s7/verified-findui_test.log
font_cli_test: exit=0 (M)[AC] BAT0=Charging log=build/edit-hgd-s7/verified-font_cli_test.log
font_selection_test: exit=0 (M)[AC] BAT0=Charging log=build/edit-hgd-s7/verified-font_selection_test.log
font_size_test: exit=0 (M)[AC] BAT0=Charging log=build/edit-hgd-s7/verified-font_size_test.log
font_test: exit=0 (M)[AC] BAT0=Charging log=build/edit-hgd-s7/verified-font_test.log
harness_test: exit=0 (M)[AC] BAT0=Charging log=build/edit-hgd-s7/verified-harness_test.log
hl_parse_test: exit=0 (M)[AC] BAT0=Charging log=build/edit-hgd-s7/verified-hl_parse_test.log
hl_test: exit=0 (M)[AC] BAT0=Charging log=build/edit-hgd-s7/verified-hl_test.log
hl_worker_test: exit=0 (M)[AC] BAT0=Charging log=build/edit-hgd-s7/verified-hl_worker_test.log
indent_test: exit=0 (M)[AC] BAT0=Charging log=build/edit-hgd-s7/verified-indent_test.log
ipc_test: exit=0 (M)[AC] BAT0=Charging log=build/edit-hgd-s7/verified-ipc_test.log
journal_group_test: exit=0 (M)[AC] BAT0=Charging log=build/edit-hgd-s7/verified-journal_group_test.log
journal_kill_test: exit=0 (M)[AC] BAT0=Charging log=build/edit-hgd-s7/verified-journal_kill_test.log
journal_retire_test: exit=0 (M)[AC] BAT0=Charging log=build/edit-hgd-s7/verified-journal_retire_test.log
journal_test: exit=0 (M)[AC] BAT0=Charging log=build/edit-hgd-s7/verified-journal_test.log
keyboard_guard_test: exit=0 (M)[AC] BAT0=Charging log=build/edit-hgd-s7/verified-keyboard_guard_test.log
keys_test: exit=0 (M)[AC] BAT0=Charging log=build/edit-hgd-s7/verified-keys_test.log
layout_test: exit=0 (M)[AC] BAT0=Charging log=build/edit-hgd-s7/verified-layout_test.log
lineidx_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/retry-lineidx_test.log
minimap_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/verified-minimap_test.log
piece_bounds_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/verified-piece_bounds_test.log
piece_mem_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/verified-piece_mem_test.log
piece_mt_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/verified-piece_mt_test.log
piece_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/verified-piece_test.log
prewake_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/verified-prewake_test.log
raster_scroll_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/verified-raster_scroll_test.log
render_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/verified-render_test.log
replay_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/verified-replay_test.log
runtime_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/verified-runtime_test.log
savectl_reload_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/verified-savectl_reload_test.log
savectl_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/verified-savectl_test.log
scan_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/verified-scan_test.log
scroll_display_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/verified-scroll_display_test.log
scroll_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/verified-scroll_test.log
startup_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/verified-startup_test.log
tabs_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/verified-tabs_test.log
trace_pairs_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/verified-trace_pairs_test.log
trace_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/verified-trace_test.log
undo_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/verified-undo_test.log
unicode_render_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/verified-unicode_render_test.log
utf8_tables_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/verified-utf8_tables_test.log
utf8_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/verified-utf8_test.log
view_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/verified-view_test.log
wayland_bench_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/verified-wayland_bench_test.log
wayland_gl_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/verified-wayland_gl_test.log
wayland_startup_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/verified-wayland_startup_test.log
wayland_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/verified-wayland_test.log
window_size_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/verified-window_size_test.log
work_foreground_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/retry-work_foreground_test.log
work_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/verified-work_test.log
wrap_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/verified-wrap_test.log
editor_checkpoint_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/verified-editor_checkpoint_test.log
editor_hl_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/verified-editor_hl_test.log
editor_hot_exit_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/verified-editor_hot_exit_test.log
editor_large_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/retry-editor_large_test.log
editor_scroll_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/verified-editor_scroll_test.log
editor_session_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/verified-editor_session_test.log
editor_undo_branch_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/verified-editor_undo_branch_test.log
gl_bench_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/verified-gl_bench_test.log
gl_gate_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/verified-gl_gate_test.log
x11_display_guard_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/verified-x11_display_guard_test.log
editor_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/verified-editor_test.log
editor_p4_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/verified-editor_p4_test.log
editor_close_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/verified-editor_close_test.log
release-editor_p4_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/verified-release-editor_p4_test.log
test_replay_cli.sh: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/verified-test_replay_cli.sh.log
```

The headless sanitizer editor run includes the new regressions and ended:

```text
editor_test: scrolled undo/redo viewport repair passed
editor_test: mutation error is negative and stops the loop passed
editor_test: null script and journal byte model passed
editor_test: null 10000 keys mallocs=0 guard=ASan-inert
editor_test: null hl-on 10000 keys mallocs=0 guard=ASan-inert
editor_test: idle blink timeout and unfocused timer disarm passed
editor_test: queue depth one and coalesced edits passed
editor_test: headless passed
exit=0 (M)[AC]
```

The headless release P4 suite confirms its allocation guard remains active:

```text
editor_p4_test: null IPC columns visible/bounded at certified clusters passed
editor_p4_test: null interrupted index recovery/stale indicator passed (stale frames=3)
editor_p4_test: null 100 tabs 10000 mixed keys all modules mallocs=0 guard=active
editor_p4_test: all passed
exit=0 (M)[AC]
```

## Prerequisites and findings outside the bead

The standard `/tmp/edit-corpus` directory was initially absent. Consequently:

- `lineidx_test` failed its mmap-ready setup in `test_combined_ownership` and LeakSanitizer reported a file allocation left by that early test return.
- `work_foreground_test` could not create its files under the missing directory; its existing nonfatal assertions allowed a later null file access.
- `editor_large_test` failed the required `log_1g.txt` stat.

These test setup/cleanup behaviors were left unchanged. The ordinary generator
`build/tools/mkcorpus /tmp/edit-corpus` supplied its existing default fixtures.
The additional sparse file uses (P) exactly the existing
`test_combined_ownership` size assertion: ten times the test's binary GiB
conversion, with sparse `ftruncate`, not a new workload size. After provisioning,
only the affected suites were rerun, with default leak detection intact:

```text
lineidx_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/retry-lineidx_test.log
work_foreground_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/retry-work_foreground_test.log
editor_large_test: exit=0 (M)[AC] BAT0=Full log=build/edit-hgd-s7/retry-editor_large_test.log
failures=[]
```

No production defect outside edit-hgd was changed. The sandbox restrictions,
missing corpus prerequisites and test behavior on their absence are recorded
for the coordinator.

## Remaining work

Implementation and non-display verification for edit-hgd are complete. Native
display validation remains with the coordinator on Xvfb `:99`; the sandbox
cannot connect to its X socket. The native benchmark rows likewise remain
unverified here. No coordination documents, indent range implementation,
Makefile, or unrelated source files were edited.
