# edit-d50 preverification

Release build passes. All completed socket-free sanitizer checks and release allocation checks pass. No merge artefact or code defect was identified in that coverage. Full display/IPC verification remains outstanding.

Read CLAUDE.md, including laws 2, 4 and 9, and the Makefile. Bead identity comes from the worktree directory name. No `git` or `bd` command was run. No source, test or Makefile change was made.

All exit codes below are observed process return codes (M)[AC], derived from `subprocess.run().returncode`; build exits come from the shell tool. Power was checked before each invocation: `/sys/class/power_supply/BAT0/status` was `Not charging`, mapped to [AC] by the existing harness convention. Performance output was not used to invent new gates.

## Builds

| Command | Exit | Result |
| --- | ---: | --- |
| `rm -rf build && make all` | 0 | GCC release, project warning flags including `-Werror`; clean build. |
| `make` with every `build/san/tests/<stem>` derived from `tests/*_test.c` | 0 | All sanitizer binaries compile/link with the Makefile ASan/UBSan flags. |

Compiler identities came from `gcc --version` and `clang --version`: GCC 13.3.0 and Clang 18.1.3. Build logs: [release](../../build/preverify/make-all.log), [sanitizers](../../build/preverify/make-san.log).

## Invocation policy and sandbox adaptation

The default binary invocations match `check`: no arguments, safe `DISPLAY`, and `sh tools/with_display_lock.sh` for the Makefile display-lane matches. The safe display was initially `:99`. Remaining pure checks used `EDIT_DISPLAY=:19950 DISPLAY=` to select a separate lock namespace through the same SAFE_DISPLAY policy after the shared lane stayed occupied. The table records each effective display. Neither name supplied an accessible display socket.

Initial default-environment attempts failed at LeakSanitizer teardown:

| Suite | Initial exit |
| --- | ---: |
| `base_test` | 1 |
| `config_editor_test` | 1 |
| `config_test` | 1 |
| `config_worker_test` | 1 |

All initial failures had this diagnostic:

```text
LeakSanitizer has encountered a fatal error.
HINT: LeakSanitizer does not work under ptrace (strace, gdb, etc)
```

Reran with `ASAN_OPTIONS=detect_leaks=0`; ASan and UBSan stayed enabled. Leak detection is unverified in this sandbox. Interrupted runner processes while they were waiting on the shared lock; those interrupted invocations did not produce a completed suite exit. Completed results were retained, and no passing suite was repeated just to change lanes.

## Default sanitizer suite runs

Each row invokes `build/san/tests/<suite>` with the policy above and `ASAN_OPTIONS=detect_leaks=0`. Successful mixed GL coverage excludes its explicitly skipped native cases.

| Suite | Exit | DISPLAY | Coverage note |
| --- | ---: | --- | --- |
| `base_test` | 0 | `:99` | PASS |
| `config_editor_test` | 0 | `:99` | PASS |
| `config_test` | 0 | `:99` | PASS |
| `config_worker_test` | 0 | `:99` | PASS |
| `editor_scroll_test` | 0 | `:99` | PASS |
| `editor_session_test` | 0 | `:99` | PASS |
| `file_kill_test` | 1 | `:99` | ENVIRONMENT: native viewport requires X11; see assertions below. |
| `file_test` | 0 | `:99` | PASS |
| `find_bench_test` | 0 | `:99` | PASS |
| `find_bestof_test` | 0 | `:99` | PASS |
| `find_p1r_test` | 0 | `:99` | PASS |
| `find_simd_filter_verify_test` | 0 | `:99` | PASS |
| `find_test` | 0 | `:99` | PASS |
| `find_timeout_test` | 0 | `:99` | PASS |
| `findui_test` | 0 | `:99` | PASS |
| `font_size_test` | 0 | `:99` | PASS |
| `font_test` | 0 | `:99` | PASS |
| `gl_bench_test` | 0 | `:99` | Pure driver/cleanup controls pass; native unsupported paths SKIP. |
| `gl_gate_test` | 0 | `:99` | PASS |
| `gl_scroll_test` | 0 | `:99` | PASS |
| `gl_test` | 0 | `:99` | Pure checks pass; native diagnostics and native acquisition stages SKIP. |
| `harness_test` | 0 | `:99` | PASS |
| `hl_parse_test` | 0 | `:99` | PASS |
| `hl_test` | 0 | `:99` | PASS |
| `hl_worker_test` | 0 | `:99` | PASS |
| `indent_test` | 0 | `:99` | PASS |
| `journal_kill_test` | 0 | `:99` | PASS |
| `journal_test` | 0 | `:99` | PASS |
| `keys_test` | 0 | `:99` | PASS |
| `layout_test` | 0 | `:99` | PASS |
| `lineidx_test` | 0 | `:99` | PASS |
| `minimap_test` | 0 | `:99` | PASS |
| `piece_bounds_test` | 0 | `:99` | PASS |
| `piece_mem_test` | 0 | `:99` | PASS |
| `piece_mt_test` | 0 | `:99` | PASS |
| `piece_test` | 0 | `:99` | PASS |
| `prewake_test` | 0 | `:99` | PASS |
| `raster_scroll_test` | 0 | `:99` | PASS |
| `raster_test` | 1 | `:19950` | ENVIRONMENT: review visual requires X11 before later skip guard. |
| `render_test` | 0 | `:19950` | PASS |
| `replay_test` | 0 | `:19950` | PASS |
| `runtime_test` | 0 | `:19950` | PASS |
| `savectl_reload_test` | 0 | `:19950` | PASS |
| `savectl_test` | 0 | `:19950` | PASS |
| `scan_test` | 0 | `:19950` | PASS |
| `scroll_display_test` | 0 | `:19950` | PASS |
| `scroll_test` | 0 | `:19950` | PASS |
| `startup_test` | 0 | `:19950` | PASS |
| `tabs_test` | 0 | `:19950` | PASS |
| `trace_test` | 0 | `:19950` | PASS |
| `undo_test` | 0 | `:19950` | PASS |
| `unicode_render_test` | 0 | `:19950` | PASS |
| `utf8_tables_test` | 0 | `:19950` | PASS |
| `utf8_test` | 0 | `:19950` | PASS |
| `view_test` | 0 | `:19950` | PASS |
| `window_size_test` | 0 | `:19950` | PASS |
| `work_foreground_test` | 0 | `:19950` | PASS |
| `work_test` | 0 | `:19950` | PASS |
| `wrap_test` | 0 | `:19950` | PASS |
| `x11_alignment_test` | 0 | `:19950` | PASS |
| `x11_display_guard_test` | 0 | `:19950` | PASS |
| `x11_input_test` | 0 | `:19950` | PASS |
| `x11_xi2_test` | 0 | `:19950` | PASS |

## Additional socket-free sanitizer selectors

`editor_test` has no `--headless` dispatch in this tree (`tests/editor_test.c:2537`). Used its existing selectors instead; the full default stays skipped. For selector rows, append the shown arguments to `build/san/tests/<suite>`, with the same wrapper policy. Environment selections are shown explicitly.

| Invocation | Exit |
| --- | ---: |
| `build/san/tests/editor_test --minimap-pixels` | 0 |
| `build/san/tests/editor_test --minimap-flash` | 0 |
| `build/san/tests/editor_test --selection-chords` | 0 |
| `build/san/tests/editor_test --redo-or-repeat` | 0 |
| `build/san/tests/editor_test --panel-state` | 0 |
| `build/san/tests/editor_test --panel-notice` | 0 |
| `build/san/tests/editor_test --clipboard-bulk` | 0 |
| `build/san/tests/editor_test --clipboard-primary` | 0 |
| `build/san/tests/editor_test --clipboard-no-tabs` | 0 |
| `build/san/tests/editor_test --ck4-target` | 0 |
| `build/san/tests/editor_test --ck4-fifo` | 0 |
| `build/san/tests/editor_test --ck4-metadata` | 0 |
| `build/san/tests/editor_test --ck4-credit` | 0 |
| `build/san/tests/editor_test --ck4-startup` | 0 |
| `build/san/tests/editor_test --ck4-save` | 0 |
| `build/san/tests/editor_test --ck4-keep` | 0 |
| `build/san/tests/editor_test --ck4-reload` | 0 |
| `build/san/tests/editor_test --ck4-mapped-keep` | 0 |
| `build/san/tests/editor_test --ck4-mapped-reload` | 0 |
| `build/san/tests/editor_test --index-capacity` | 0 |
| `build/san/tests/editor_test --index-refusal` | 0 |
| `build/san/tests/editor_test --index-repair` | 0 |
| `build/san/tests/editor_test --line-start-only` | 0 |
| `build/san/tests/editor_test --selection` | 0 |
| `build/san/tests/editor_test --fallback-headless` | 0 |
| `build/san/tests/editor_test --gpu-completion` | 0 |
| `EDITOR_ALLOC_ONLY=null build/san/tests/editor_test` | 0 |
| `build/san/tests/editor_p4_test --indent-limit-enter` | 0 |
| `build/san/tests/editor_p4_test --indent-limit-brace` | 0 |
| `build/san/tests/x11_clip_review_test --allocator-only` | 0 |
| `build/san/tests/ipc_test parser` | 0 |
| `build/san/tests/ipc_test wire` | 0 |
| `build/san/tests/ipc_test result_text` | 0 |
| `build/san/tests/ipc_test parser_paths` | 0 |
| `build/san/tests/ipc_test wire_table` | 0 |
| `ASAN_OPTIONS=detect_leaks=0 build/san/tests/raster_test --review 5` | 0 |
| `ASAN_OPTIONS=detect_leaks=0 build/san/tests/raster_test --review 6` | 0 |
| `ASAN_OPTIONS=detect_leaks=0 build/san/tests/raster_test --review 7` | 0 |
| `ASAN_OPTIONS=detect_leaks=0 build/san/tests/raster_test --review 8` | 0 |
| `ASAN_OPTIONS=detect_leaks=0 build/san/tests/raster_test --review 15` | 0 |
| `ASAN_OPTIONS=detect_leaks=0 build/san/tests/raster_test --review 17` | 0 |
| `ASAN_OPTIONS=detect_leaks=0 build/san/tests/raster_test --review G11` | 0 |

## Release allocation verification

The sanitizer allocator guard reports itself inert because ASan owns malloc. These existing GCC-release tests exercise the counting guard with the same socket-free fixtures; no tests were added.

| Invocation | Exit |
| --- | ---: |
| `build/tests/base_test` | 0 |
| `build/tests/keys_test` | 0 |
| `build/tests/indent_test` | 0 |
| `build/tests/view_test` | 0 |
| `build/tests/wrap_test` | 0 |
| `EDITOR_ALLOC_ONLY=null build/tests/editor_test` | 0 |
| `build/tests/editor_test --selection-chords` | 0 |
| `build/tests/editor_test --redo-or-repeat` | 0 |
| `build/tests/editor_test --panel-state` | 0 |
| `build/tests/editor_test --panel-notice` | 0 |

## Shell contracts from check

| Invocation | Exit |
| --- | ---: |
| `sh tools/test_replay_cli.sh build` | 0 |
| `sh tools/test_display_lock.sh contract build/san/tests/refwin_test` | SKIP: no completed suite exit; shared display lock occupied |

The display-lock contract hardcodes `:99` internally. Its holder log reports `waiting for display :99 (held by unknown)`; no ready marker was produced, so no contract assertion ran. Stopped the blocked extra runner (observed runner exit 130), without attributing that exit to the suite. This shell contract remains for the coordinator to run when the shared lane is free.

## Skipped full suites and native coverage

| Suite | Why full coverage is unavailable |
| --- | --- |
| `cli_test` | Live X11 window discovery and IPC sockets. |
| `editor_close_test` | Default includes native WM_DELETE_WINDOW after injected close. |
| `editor_large_test` | Default includes mapped_startup_ipc and Unix client/server sockets. |
| `editor_p4_test` | Default creates an IPC server and exercises native raster rendering. |
| `editor_test` | No --headless dispatch; default includes native clipboard, rendering and input. |
| `ipc_test` | Default requires Unix filesystem/abstract sockets and live display launcher tests. |
| `refproto_test` | refproto_display_open creates a real X11 window. |
| `refwin_test` | Requires shared Xvfb :99 and real editor/reference windows. |
| `refwin_timeout_test` | Child refwin must open X11 before testing missing KeyPress timeout. |
| `x11_clip_review_test` | Default allocator adoption and clipboard cases connect to X11 :99. |
| `x11_clip_test` | Starts/connects private Xvfb for live selections. |
| `x11_font_size_test` | Reads native display geometry and opens raster window. |
| `x11_identity_test` | Requires X11 :99 for window property readback. |
| `x11_live_test` | Starts/connects private Xvfb. |
| `x11_order_test` | Starts/connects private Xvfb. |
| `x11_stall_test` | Starts/connects private Xvfb and live callback round trips. |
| `file_kill_test` native viewport/queued-open/queued-save | Indirect `bench/file_bench.c:304` calls `plat_init`; attempted default exited as recorded. No headless selector exists. |
| `raster_test` native visual, native conformance/upload/fence/resize | `review_visual` calls `plat_init` before the later display guard. Pure review selectors after that failure were run separately. Kernel random/blend/partition calls after `review_cases("all")` were not reached by the default. |
| `gl_test` native diagnostics/readback/presentation; native acquisition fault stages | Suite explicitly reports unavailable X11/EGL prerequisites; pure checks completed. |

The sandbox has no accessible X/Wayland display socket or IPC socket. Socket-free IPC parser/wire selectors and clipboard allocator checks above do not certify socket transport or live selections.

## Environmental assertion output

These failures identify missing display prerequisites, not merge artefacts or demonstrated bead design defects. No source fix was applied.

`file_kill_test`, observed exit as above:

```text
file_kill_test:198: FAIL rc == 0
file_kill_test:324: FAIL WIFEXITED(status) && WEXITSTATUS(status) == 0
file_kill_test:324: FAIL WIFEXITED(status) && WEXITSTATUS(status) == 0
file_kill_test: FAIL visibility/cancellation; power-loss barriers unverified
```

Derivation: `view_init` at `bench/file_bench.c:285` opens X11 at line 304; `file_test_viewport` and both queued children use it. Its printed cancellation, save-allocation and adversarial self-check portions passed before the aggregate failure.

`raster_test`, observed exit as above:

```text
raster_test:1125: FAIL plat_init(&pl, &pc) == PLAT_OK
P2.5b section 3: RED
```

Derivation: native `review_visual` is dispatched inside `review_cases("all")` before the default no-display guard at `tests/raster_test.c:576`. The region snapshot and preceding pure reviews passed; remaining selectable pure reviews passed separately.

Some zero-exit suites deliberately print FAIL/RED diagnostics for negative controls (for example find timeout and benchmark controls). The parent exit and explicit control assertions determine their result, as in `make check`. Completed adapted logs contain no `runtime error:`, `ERROR: AddressSanitizer`, or ASan/UBSan error summaries.

## Fixes applied

None. No failing code assertion was a demonstrated merge artefact requiring a minimal source repair. There is no source red/green cycle to report; sandbox adaptation and its rerun evidence are listed above.

## Defects and remaining coordinator verification

No code defect was found in the completed headless coverage. Run the skipped full display/IPC suites, including native `file_kill_test` and `raster_test`, in the coordinator environment, plus the display-lock shell contract when the shared lane is free. LeakSanitizer must be checked outside the ptrace-restricted sandbox. The results above do not certify a full `make check`.

Per-invocation output and observed exits are retained in [build/preverify](../../build/preverify/results.json) and [supplementary results](../../build/preverify/supplementary-results.json); each result names its `.log` file. Initial LeakSanitizer logs are in [build/preverify-lsan-default](../../build/preverify-lsan-default/results.json).
