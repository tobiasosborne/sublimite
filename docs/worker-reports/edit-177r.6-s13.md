# edit-177r.6-s13 worker report

Verdict: **done**. Inventory recorded before editing in `docs/decisions/edit-177r.6.md`.

## Scope and changes

Config reads, parent creation and identity checks use os.h; platform files retain the Linux resolver/aligned allocator and select RoamingAppData plus the Windows aligned allocator. Fif uses os_dir plus a platform iterator helper (Windows os_dir_iter; Linux retains native checked closedir/close), uses os_identity and parent-relative stat/open, preserves symlink/root policy, and uses OS_PATH_MAX on both platforms. Findui’s setup-only unnamed spools, positional transfers, truncate, clock and worker-active checks are portable. Tabs asks os_page_size. No config_runtime_wake was added (W2.4c ownership).

Port config, fif, findui and tabs OS calls; add the brief’s nine dependency modules and applicable test suites to the Windows build. Pure modules stay unchanged. No editor/main, frozen os.h, HANDOFF or worklog edits.

Files touched: `src/config/{config.h,private.h,runtime.c,config_posix.c,config_win.c}`, `src/fif/{fif.c,fif.h,private.h,filter.c,worker.c,fif_posix.c,fif_win.c}`, `src/findui/{findui.c,private.h,plan.c,worker.c}`, `src/tabs/tabs.c`, `tests/{config,fif,findui,indent,keys,minimap}_test.c`, `tests/win/{dependency_fixture,dependency_vm,fif_fixture}.h`, `mk/win.mk`, and this report/decision doc. `mk/win.mk` changes are confined to module/test lists, removing the now-duplicated theme source, per-test stack rules, and dependency summary/skip recipe lines. No src/editor/private.h edit.

## Windows coverage and explicit skips

| Suites | Coverage / reason for skip |
|---|---|
| config, config_worker | Parser/settings/keymap/cancellation expectations retained; runtime reads and create/replace/delete notices use isolated NTFS scratch. Directory-as-file remains CONFIG_ERR_IO. config_editor/config_find require the unported editor and are explicitly skipped in the make summary. |
| fif | Existing ordered hits, limits, cancellation and allocation expectations retained; junctions cover root-follow and descendant-skip. POSIX chmod unreadability is skipped with its native-suite reason. Added native absolute/backslash/trailing-root regression. |
| findui | Standalone query, worker, replacement/undo/journal recovery and protected-page/spool cases retained. Only the editor clipboard/chord integration is skipped, with a reason. |
| keys | Independent bindings and allocation guard retained; existing Linux-only libxkbcommon differential oracle reports its skip. |
| tabs, menu, minimap, palette, indent | Existing expectations retained; Windows VirtualAlloc/VirtualProtect fixtures preserve inaccessible-page work oracles where needed. |

## Red / green

(M) [AC, desktop Spiral] `make TARGET=win -j64 win-compile`, exit **2**. Uptime: `19:10:54 up 11:18,  1 user,  load average: 0.98, 1.20, 0.68`. Full output: `build/worker-logs/win-red.log`.

```text
src/config/runtime.c:45:25: error: too many arguments to function call, expected 1, have 2
src/config/runtime.c:56:39: error: use of undeclared identifier 'O_CLOEXEC'
src/config/runtime.c:126:25: error: call to undeclared library function 'aligned_alloc'
src/fif/worker.c:53:15: error: call to undeclared function 'dirfd'
src/findui/plan.c:10:31: error: call to undeclared function 'pwrite'
src/tabs/tabs.c:42:15: error: call to undeclared function 'sysconf'
exit=2
```

(M) [AC, desktop Spiral] `make TARGET=win -j64 win-compile`, exit **0**, `build/worker-logs/win-compile-green.log`:

```text
19:12:22 up 11:20,  1 user,  load average: 1.04, 1.18, 0.73
... -c src/fif/worker.c -o build/win/rel/src/fif/worker.o
exit=0
```

The original tests then gave additional native reds (`build/worker-logs/win-scoped-rel.log`): fif’s absolute include expected the fixture’s four hits but matched no Windows drive prefix; findui exited 253 at its large-match fixture (objdump frame exceeds PE default). Fixes: os_path_is_absolute for root-relative filters; test-only stack reserve matching Linux. Release fif and findui and sanitizer config/findui now pass (`build/worker-logs/win-scoped-green.log`). Fif sanitizer exposed one further stack red: ASan stack-overflow, main frame `0x14f838` bytes (llvm-objdump); matching the existing layout reserve passed (`build/worker-logs/win-fif-san-green.log`, `RESULT fif sanitizer exit=0`). Full final checks passed; see verification below.

Additional test-first native path witness: `make TARGET=win build/win/rel/tests/fif_test.exe` then `tools/winrun.sh build/win/rel/tests/fif_test.exe`, exit **9** (M), `build/worker-logs/win-fif-native-red.log`:

```text
19:24:02 up 11:31,  1 user,  load average: 1.62, 4.51, 3.98
tests/fif_test.c:217: assertion failed: got.count == 4
RESULT fif native-path red exit=9
```

(E) Expected four hits are the sensitive `cat` offsets in a.txt’s fixture literal. Native drive-prefix separator handling and trailing-root trimming now match the original slash spelling. `make TARGET=win -j64 build/win/rel/tests/fif_test.exe build/win/san/tests/fif_test.exe` and native runs passed (M), `build/worker-logs/win-fif-native-green.log`:

```text
RESULT build/win/rel/tests/fif_test.exe native-path green exit=0
RESULT build/win/san/tests/fif_test.exe native-path green exit=0
```

Red inventory also exposed config’s aligned allocation and findui’s worker-active checks; these join the migration inventory.

## Verification

All final commands exited **0** (M), [AC, desktop Spiral]. BAT0 is absent; ASAN_OPTIONS and LSAN_OPTIONS are unset, retaining default Linux LeakSanitizer coverage. The suite counts below derive from literal make summaries and parsed RESULT rows.

| Command | Result | Full log |
|---|---|---|
| `make -j64 all` | Linux release build passed | `build/worker-logs/linux-all-final.log` |
| `DISPLAY=:97 make -j64 check` | 110 test binaries passed, replay CLI and display-lock contract PASS | `build/worker-logs/linux-check-final.log` |
| `make TARGET=win -j64 win-compile` | All selected module objects compiled | `build/worker-logs/win-compile-final.log` |
| `make TARGET=win -j64 check WIN_B=build/win-final` | 59 release + 59 sanitizer binaries passed; ASan negative control passed | `build/worker-logs/win-check-final.log` |

The Windows output override isolates the final build from an earlier run’s live executables; it uses the same module lists, recipes and compiler flags. (M) Parsing the final log gives 118 RESULT rows, all zero; 20 are this bead’s dependency release/sanitizer suites (including config_worker). Exact summary:

```text
Windows dependencies: config/config_worker, fif, findui, tabs, keys, menu, minimap, palette and indent suites passed
Windows dependencies: SKIP config_editor/config_find and findui panel clipboard/chords (require editor, owned by W2.4b/W2.4c); POSIX chmod and libxkbcommon oracle skipped with native-suite reasons
Windows check: 59 release + 59 sanitizer test binaries passed; ASan negative control passed
exit=0
```

Uptime/load recorded at each final command’s start (M), [AC, desktop Spiral]:

- `make -j64 all`: `19:33:45 up 11:41,  1 user,  load average: 3.56, 4.60, 4.35`
- `DISPLAY=:97 make -j64 check`: `19:34:52 up 11:42,  1 user,  load average: 3.83, 4.80, 4.46`
- `make TARGET=win -j64 win-compile`: `19:40:31 up 11:48,  1 user,  load average: 1.48, 3.54, 4.16`
- `make TARGET=win -j64 check WIN_B=build/win-final`: `19:55:38 up 12:03,  1 user,  load average: 0.37, 1.31, 2.56`

The Linux display-lock contract deliberately emits nested timeout/Error rows while testing refusal; its final contract result and the outer command pass. The first full Linux run also passed (`linux-check.log`).

An earlier final-source Windows run (`win-check-cold.log`) exited 2 solely because release win32_plat_test’s foreign clipboard child missed its existing ready-event wait, then its exit-code assertion failed. (M) That log has 118 binary RESULT rows, exactly one nonzero, all 20 dependency rows zero, and the ASan negative control fired. Both separate platform reruns passed (`win32-final-rerun.log`), then the complete unchanged-binary retry above passed. No out-of-scope platform code or timing bound was changed.

The earlier default-output Windows run passed 58 release + 58 sanitizer binaries (`win-check.log`), before config_worker and the final compatibility adapters were added. Superseded final runs were deliberately stopped with exit 143 to include the compatibility adapters (`linux-check-pre-close.log`, `win-check-pre-close.log`); these are retained for audit, not final results.

`git diff --check` and an allowlist audit of changed paths passed. No source changes followed the final check restart.

## Found, not fixed

`findui_bench --track` exits 1 before measurement because its private stamp() requires BAT0; this host has no battery. Outside the scoped production/test migration, left unchanged. Other scoped benches ran (config, tabs, keys, minimap, palette, indent and fif all exit 0; M: each RESULT row in the log) (full output in `build/worker-logs/benches.log`); all emitted measurements are (M) [AC, desktop Spiral], overriding their legacy `[unknown]` label with the verified absent-BAT0 host stamp. Their performance misses are tracked only. Fif was measured again after the compatibility adapters: `DISPLAY=:97 build/bench/fif_bench`, exit 0 (M), `build/worker-logs/fif-bench-final.log`; its counters report no I/O errors and a tracked throughput miss. No source changes followed the final check restart.

The existing Windows journal summary still says `SKIP src/file save integration (file module not ported)` even though file is already in WIN_MODULES. This is a preexisting stale summary, left outside this bead’s change; the file and production savectl suites run normally.

The final-source Windows run’s foreign clipboard child startup timeout is recorded above and passed both separate reruns; this worker does not change the platform suite’s timing bound.

## Coordinator decisions

None pending; platform choices are in the decision doc.
