# edit-5ih headless pre-verification

Release build passed. The latest valid headless sanitizer invocations passed. No source fixes were applied. Full display/IPC verification and leak checking remain for the coordinator. An initial GL cleanup assertion failed and then passed both its targeted and full-suite reruns; retain that observation when reviewing the display run.

## Scope and evidence

Read `CLAUDE.md`, including laws 2, 4 and 9, and `Makefile` before verification. Bead name derives from the worktree directory `edit-5ih`. No `git` or `bd` command was run. No source, test, or Makefile was edited.

All reported exit codes are **(M)[AC]**, obtained from subprocess completion; zero means successful termination. Power stamp came from `/sys/class/power_supply/BAT0/status` = `Not charging`, corroborated by `/sys/class/power_supply/AC/online` = `1` (AC connected). Test timing output is preserved in logs; no new performance gate or budget was introduced.

## Builds

| Command | Exit code (M)[AC] | Evidence |
| --- | ---: | --- |
| `rm -rf build && make all` | 0 | [release log](../../build/preverify/make-all.log) |
| `make` with every `build/san/tests/<name>` target generated from sorted `tests/*_test.c` | 0 | [sanitizer build log](../../build/preverify/sanitizer-build.log) |

Release used the Makefile's GCC C11 release flags, including `-Werror`. Sanitizer binaries used its Clang `-fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g` flags. Replay tooling was built by `make all`.

## Sanitizer invocation and initial environment blocker

Tests ran serially in Makefile order with `DISPLAY=:99`, `EDIT_DISPLAY=:99`; the Makefile-designated display group used `sh tools/with_display_lock.sh`. The replay CLI and display-lock contract scripts ran with the same arguments as `check`. The contract verifies lock behavior before opening a display, so it is usable here.

The initial default-sanitizer attempts failed at LeakSanitizer shutdown:

```text
LeakSanitizer has encountered a fatal error.
HINT: LeakSanitizer does not work under ptrace (strace, gdb, etc)
```

Observed initial completed attempts (before runner interruption):

| Invocation | Exit code | Evidence |
| --- | ---: | --- |
| `base_test` | 1 | [log](../../build/preverify/initial-lsan/base_test.log) |
| `config_editor_test` | 1 | [log](../../build/preverify/initial-lsan/config_editor_test.log) |
| `config_test` | 1 | [log](../../build/preverify/initial-lsan/config_test.log) |
| `config_worker_test` | 1 | [log](../../build/preverify/initial-lsan/config_worker_test.log) |
| `editor_scroll_test` | 1 | [log](../../build/preverify/initial-lsan/editor_scroll_test.log) |
| `editor_session_test` | 1 | [log](../../build/preverify/initial-lsan/editor_session_test.log) |
| `file_kill_test` | 1 | [log](../../build/preverify/initial-lsan/file_kill_test.log) |
| `file_test` | 1 | [log](../../build/preverify/initial-lsan/file_test.log) |
| `find_bench_test` | 1 | [log](../../build/preverify/initial-lsan/find_bench_test.log) |

Diagnostic runners were interrupted to establish a complete isolated rerun; their shell exit was `130` (M)[AC], derived from `128 + SIGINT(2)`. The complete run and follow-ups below use **`ASAN_OPTIONS=detect_leaks=0`**. AddressSanitizer and UndefinedBehaviorSanitizer remain enabled. LeakSanitizer was not verified; this is an environment limitation, not a leak finding.

## Default suite attempts

Names below resolve to `build/san/tests/<name>`; wrappers and exact argv are also recorded in [results.json](../../build/preverify/final/results.json). Rows with unavailable display fixtures are explained below and are not treated as design failures.

| Invocation | Exit code | Evidence |
| --- | ---: | --- |
| `base_test` | 0 | [log](../../build/preverify/final/base_test.log) |
| `config_editor_test` | 0 | [log](../../build/preverify/final/config_editor_test.log) |
| `config_test` | 0 | [log](../../build/preverify/final/config_test.log) |
| `config_worker_test` | 0 | [log](../../build/preverify/final/config_worker_test.log) |
| `editor_scroll_test` | 0 | [log](../../build/preverify/final/editor_scroll_test.log) |
| `editor_session_test` | 0 | [log](../../build/preverify/final/editor_session_test.log) |
| `file_kill_test` | 1 | [log](../../build/preverify/final/file_kill_test.log) |
| `file_test` | 0 | [log](../../build/preverify/final/file_test.log) |
| `find_bench_test` | 0 | [log](../../build/preverify/final/find_bench_test.log) |
| `find_bestof_test` | 0 | [log](../../build/preverify/final/find_bestof_test.log) |
| `find_p1r_test` | 0 | [log](../../build/preverify/final/find_p1r_test.log) |
| `find_simd_filter_verify_test` | 0 | [log](../../build/preverify/final/find_simd_filter_verify_test.log) |
| `find_test` | 0 | [log](../../build/preverify/final/find_test.log) |
| `find_timeout_test` | 0 | [log](../../build/preverify/final/find_timeout_test.log) |
| `findui_test` | 0 | [log](../../build/preverify/final/findui_test.log) |
| `font_size_test` | 0 | [log](../../build/preverify/final/font_size_test.log) |
| `font_test` | 0 | [log](../../build/preverify/final/font_test.log) |
| `gl_bench_test` | 0 | [log](../../build/preverify/final/gl_bench_test.log) |
| `gl_gate_test` | 0 | [log](../../build/preverify/final/gl_gate_test.log) |
| `gl_scroll_test` | 0 | [log](../../build/preverify/final/gl_scroll_test.log) |
| `gl_test` | 1 | [log](../../build/preverify/final/gl_test.log) |
| `harness_test` | 0 | [log](../../build/preverify/final/harness_test.log) |
| `hl_parse_test` | 0 | [log](../../build/preverify/final/hl_parse_test.log) |
| `hl_test` | 0 | [log](../../build/preverify/final/hl_test.log) |
| `hl_worker_test` | 0 | [log](../../build/preverify/final/hl_worker_test.log) |
| `indent_test` | 0 | [log](../../build/preverify/final/indent_test.log) |
| `journal_kill_test` | 0 | [log](../../build/preverify/final/journal_kill_test.log) |
| `journal_test` | 0 | [log](../../build/preverify/final/journal_test.log) |
| `keys_test` | 0 | [log](../../build/preverify/final/keys_test.log) |
| `layout_test` | 0 | [log](../../build/preverify/final/layout_test.log) |
| `lineidx_test` | 0 | [log](../../build/preverify/final/lineidx_test.log) |
| `minimap_test` | 0 | [log](../../build/preverify/final/minimap_test.log) |
| `piece_bounds_test` | 0 | [log](../../build/preverify/final/piece_bounds_test.log) |
| `piece_mem_test` | 0 | [log](../../build/preverify/final/piece_mem_test.log) |
| `piece_mt_test` | 0 | [log](../../build/preverify/final/piece_mt_test.log) |
| `piece_test` | 0 | [log](../../build/preverify/final/piece_test.log) |
| `prewake_test` | 0 | [log](../../build/preverify/final/prewake_test.log) |
| `raster_scroll_test` | 0 | [log](../../build/preverify/final/raster_scroll_test.log) |
| `raster_test` | 1 | [log](../../build/preverify/final/raster_test.log) |
| `refproto_test` | 1 | [log](../../build/preverify/final/refproto_test.log) |
| `render_test` | 0 | [log](../../build/preverify/final/render_test.log) |
| `replay_test` | 0 | [log](../../build/preverify/final/replay_test.log) |
| `runtime_test` | 0 | [log](../../build/preverify/final/runtime_test.log) |
| `savectl_reload_test` | 0 | [log](../../build/preverify/final/savectl_reload_test.log) |
| `savectl_test` | 0 | [log](../../build/preverify/final/savectl_test.log) |
| `scan_test` | 0 | [log](../../build/preverify/final/scan_test.log) |
| `scroll_display_test` | 0 | [log](../../build/preverify/final/scroll_display_test.log) |
| `scroll_test` | 0 | [log](../../build/preverify/final/scroll_test.log) |
| `startup_test` | 0 | [log](../../build/preverify/final/startup_test.log) |
| `tabs_test` | 0 | [log](../../build/preverify/final/tabs_test.log) |
| `trace_test` | 0 | [log](../../build/preverify/final/trace_test.log) |
| `undo_test` | 0 | [log](../../build/preverify/final/undo_test.log) |
| `unicode_render_test` | 0 | [log](../../build/preverify/final/unicode_render_test.log) |
| `utf8_tables_test` | 0 | [log](../../build/preverify/final/utf8_tables_test.log) |
| `utf8_test` | 0 | [log](../../build/preverify/final/utf8_test.log) |
| `view_test` | 0 | [log](../../build/preverify/final/view_test.log) |
| `window_size_test` | 0 | [log](../../build/preverify/final/window_size_test.log) |
| `work_foreground_test` | 0 | [log](../../build/preverify/final/work_foreground_test.log) |
| `work_test` | 0 | [log](../../build/preverify/final/work_test.log) |
| `wrap_test` | 0 | [log](../../build/preverify/final/wrap_test.log) |
| `x11_alignment_test` | 0 | [log](../../build/preverify/final/x11_alignment_test.log) |
| `x11_display_guard_test` | 0 | [log](../../build/preverify/final/x11_display_guard_test.log) |
| `x11_input_test` | 0 | [log](../../build/preverify/final/x11_input_test.log) |
| `x11_xi2_test` | 0 | [log](../../build/preverify/final/x11_xi2_test.log) |

## Additional headless dispatches and follow-ups

This tree has no `editor_test --headless` dispatch. The existing headless editor flags were exercised individually instead. Socket-free IPC parser/wire cases and mixed-suite headless dispatches were also exercised. The original raster `--pixel-region` probe was an invocation mistake: that flag falls through to the default dispatch; the corrected `--pixels-only` row is the valid probe.

| Invocation | Exit code | Evidence |
| --- | ---: | --- |
| `editor_test --selection` | 0 | [log](../../build/preverify/final/editor_test--selection.log) |
| `editor_test --fallback-headless` | 0 | [log](../../build/preverify/final/editor_test--fallback-headless.log) |
| `editor_test --gpu-completion` | 0 | [log](../../build/preverify/final/editor_test--gpu-completion.log) |
| `editor_test --selection-chords` | 0 | [log](../../build/preverify/final/editor_test--selection-chords.log) |
| `editor_test --redo-or-repeat` | 0 | [log](../../build/preverify/final/editor_test--redo-or-repeat.log) |
| `editor_test --panel-state` | 0 | [log](../../build/preverify/final/editor_test--panel-state.log) |
| `editor_test --panel-notice` | 0 | [log](../../build/preverify/final/editor_test--panel-notice.log) |
| `editor_test --clipboard` | 0 | [log](../../build/preverify/final/editor_test--clipboard.log) |
| `editor_test --ck4-target` | 0 | [log](../../build/preverify/final/editor_test--ck4-target.log) |
| `editor_test --ck4-fifo` | 0 | [log](../../build/preverify/final/editor_test--ck4-fifo.log) |
| `editor_test --ck4-metadata` | 0 | [log](../../build/preverify/final/editor_test--ck4-metadata.log) |
| `editor_test --ck4-credit` | 0 | [log](../../build/preverify/final/editor_test--ck4-credit.log) |
| `editor_test --ck4-startup` | 0 | [log](../../build/preverify/final/editor_test--ck4-startup.log) |
| `editor_test --ck4-save` | 0 | [log](../../build/preverify/final/editor_test--ck4-save.log) |
| `editor_test --ck4-keep` | 0 | [log](../../build/preverify/final/editor_test--ck4-keep.log) |
| `editor_test --ck4-reload` | 0 | [log](../../build/preverify/final/editor_test--ck4-reload.log) |
| `editor_test --ck4-mapped-keep` | 0 | [log](../../build/preverify/final/editor_test--ck4-mapped-keep.log) |
| `editor_test --ck4-mapped-reload` | 0 | [log](../../build/preverify/final/editor_test--ck4-mapped-reload.log) |
| `editor_test --index-capacity` | 0 | [log](../../build/preverify/final/editor_test--index-capacity.log) |
| `editor_test --index-refusal` | 0 | [log](../../build/preverify/final/editor_test--index-refusal.log) |
| `editor_test --index-repair` | 0 | [log](../../build/preverify/final/editor_test--index-repair.log) |
| `editor_test --line-start-only` | 0 | [log](../../build/preverify/final/editor_test--line-start-only.log) |
| `editor_test --minimap-cost` | 0 | [log](../../build/preverify/final/editor_test--minimap-cost.log) |
| `editor_p4_test --indent-limit-enter` | 0 | [log](../../build/preverify/final/editor_p4_test--indent-limit-enter.log) |
| `editor_p4_test --indent-limit-brace` | 0 | [log](../../build/preverify/final/editor_p4_test--indent-limit-brace.log) |
| `ipc_test parser` | 0 | [log](../../build/preverify/final/ipc_test-parser.log) |
| `ipc_test wire` | 0 | [log](../../build/preverify/final/ipc_test-wire.log) |
| `ipc_test result_text` | 0 | [log](../../build/preverify/final/ipc_test-result_text.log) |
| `ipc_test wire_alias` | 0 | [log](../../build/preverify/final/ipc_test-wire_alias.log) |
| `ipc_test wire_table` | 0 | [log](../../build/preverify/final/ipc_test-wire_table.log) |
| `ipc_test parser_paths` | 0 | [log](../../build/preverify/final/ipc_test-parser_paths.log) |
| `sh tools/test_replay_cli.sh build` | 0 | [log](../../build/preverify/final/test_replay_cli.log) |
| `sh tools/test_display_lock.sh contract build/san/tests/refwin_test` | 0 | [log](../../build/preverify/final/test_display_lock-contract.log) |
| `FILE_KILL_CANCEL_ONLY=1 file_kill_test` | 0 | [log](../../build/preverify/final/file_kill_test-cancel-only.log) |
| `gl_test --review p2-5` | 0 | [log](../../build/preverify/final/gl_test-cleanup-rerun.log) |
| `gl_test` | 0 | [log](../../build/preverify/final/gl_test-full-rerun.log) |
| `raster_test --review P2-1-15` | 0 | [log](../../build/preverify/final/raster_test-review-P2-1-15.log) |
| `raster_test --review P2-1-11` | 0 | [log](../../build/preverify/final/raster_test-review-P2-1-11.log) |
| `raster_test --review P2-1-8` | 0 | [log](../../build/preverify/final/raster_test-review-P2-1-8.log) |
| `raster_test --review P2-1-7` | 0 | [log](../../build/preverify/final/raster_test-review-P2-1-7.log) |
| `raster_test --review 1` | 0 | [log](../../build/preverify/final/raster_test-review-1.log) |
| `raster_test --review 4` | 0 | [log](../../build/preverify/final/raster_test-review-4.log) |
| `raster_test --review 2` | 0 | [log](../../build/preverify/final/raster_test-review-2.log) |
| `raster_test --review 5` | 0 | [log](../../build/preverify/final/raster_test-review-5.log) |
| `raster_test --review 6` | 0 | [log](../../build/preverify/final/raster_test-review-6.log) |
| `raster_test --review 7` | 0 | [log](../../build/preverify/final/raster_test-review-7.log) |
| `raster_test --review 8` | 0 | [log](../../build/preverify/final/raster_test-review-8.log) |
| `raster_test --review 15` | 0 | [log](../../build/preverify/final/raster_test-review-15.log) |
| `raster_test --review 17` | 0 | [log](../../build/preverify/final/raster_test-review-17.log) |
| `raster_test --review G11` | 0 | [log](../../build/preverify/final/raster_test-review-G11.log) |
| `raster_test --pixel-region` | 1 | [log](../../build/preverify/final/raster_test-pixel-region.log) |
| `raster_test --pixels-only` | 0 | [log](../../build/preverify/final/raster_test-pixels-only.log) |

## Suites skipped

The sandbox has no accessible X11 socket and cannot provide the Unix IPC endpoints these suites require. These are skipped default dispatches, not passing results. Headless subsets, where exposed, appear above.

| Suite | Reason |
| --- | --- |
| `cli_test` | requires live X11 and IPC sockets |
| `editor_close_test` | default dispatch includes native WM_DELETE_WINDOW/X11 case; no headless-only flag |
| `editor_large_test` | default dispatch includes mapped_startup_ipc and a Unix client/server socket |
| `editor_p4_test` | default dispatch initializes IPC server and includes raster/X11 cases |
| `editor_test` | default dispatch includes live GL/raster/X11; no --headless flag in this tree |
| `ipc_test` | requires Unix IPC sockets; default dispatch also includes a live display case |
| `refwin_test` | requires live reference windows and X11 input/Present |
| `refwin_timeout_test` | requires a live X11 reference window before testing missing KeyPress |
| `x11_clip_review_test` | connects to :99 for clipboard fixtures |
| `x11_clip_test` | starts private Xvfb and connects through X11 sockets |
| `x11_font_size_test` | opens native CPU-rendered editor and uses X11 |
| `x11_identity_test` | requires plat_init with a live :99 window |
| `x11_live_test` | starts private Xvfb and connects through X11 sockets |
| `x11_order_test` | starts private Xvfb and connects through X11 sockets |
| `x11_stall_test` | starts private Xvfb and connects through X11 sockets |

Mixed suites attempted before their hidden display prerequisites were identified:

| Suite/cases left unverified | Reason and observed output |
| --- | --- |
| `file_kill_test` viewport and queued renderer cases | Includes `bench/file_bench.c`; `view_init` calls `plat_init` at `bench/file_bench.c:304`. Default exited 1; cancellation-only exited 0. |
| `raster_test` live visual, native resize/upload/fence paths; default-path kernel/blend/partition checks after the live visual dispatch | Default review section 3 calls `plat_init` at `tests/raster_test.c:1125` and returns before the later checks. Exposed headless review cases and `--pixels-only` passed. |
| `refproto_test` | Scripted clock replies still require a real window/focus/version preflight. Exited 1 at `tests/refproto_test.c:62`; no headless-only dispatcher. |
| `gl_test` native diagnostics/lifecycle and native benchmark acquisition stages | Full rerun explicitly reported native prerequisites unavailable and `PASS (pure snapshot/completion/trace checks; native diagnostics SKIP)`. |

ASan owns malloc, so libc allocation-count hooks are inactive in the sanitizer configuration. Their internal skip messages are preserved in the base/font/render/trace/work/X11-input/journal/line-index logs. No release allocation-guard run is claimed here.

## Assertions and disposition

Display limitations, left for the coordinator's display run:

```text
file_kill_test:198: FAIL rc == 0
file_kill_test:324: FAIL WIFEXITED(status) && WEXITSTATUS(status) == 0
file_kill_test:324: FAIL WIFEXITED(status) && WEXITSTATUS(status) == 0
raster_test:1125: FAIL plat_init(&pl, &pc) == PLAT_OK
refproto_test: window=0x00000000 connect DISPLAY=:99: connection error=1
refproto_test:62: FAIL refproto_display_open(d,"refproto_test",true) == 0
```

Headless GL cleanup observation, left for coordinator awareness:

```text
gl_bench: INJECTED stage=self-overlap
gl_test:1405: FAIL gl_live_threads()==before
gl_test:1547: FAIL gl_bench_cleanup_test()==0
```

The initial `gl_test` exited 1. `gl_test --review p2-5` subsequently exited 0, and a full `gl_test` rerun exited 0. This did not reproduce; its cause is undetermined. No implementation or test change was made to suppress it.

## Fixes applied

None. No renamed-field/signature mismatch, duplicate-dispatch build failure, or other identifiable pure merge artefact required repair. No new regression test was written, so a source red/green cycle is not applicable; the observed failed and repeated successful invocations are recorded above.

## Defects left for the coordinator

No reproducible bead-specific design defect was found by the available headless checks. Review the transient GL cleanup assertion, run the skipped/partial native and IPC suites, and repeat leak checking outside the ptrace-restricted sandbox before treating full `make check` as green.

Raw build/test evidence and verifier scripts are under [build/preverify](../../build/preverify/).
