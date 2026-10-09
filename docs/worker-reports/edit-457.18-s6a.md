Implemented Sublimité’s runtime identity, the display opt-in fix, and shutdown trace dumping.

Changed files: `Makefile`, `src/main.c`; editor, X11, IPC and journal sources/headers/status files; IPC/editor tests plus new runtime, X11 identity and CLI tests; `bench/ipc_bench.c`; CI, installer and desktop contract tools. Renamed `tools/edit.desktop` to `tools/sublimite.desktop`. Details: [rename-sublimite.md](/home/tobias/Projects/editor/.wt/edit-457.18/docs/decisions/rename-sublimite.md).

Red run:

```text
x11_identity_test:18: FAIL xcb_get_property_value_length(r) == (int)len
x11_identity_test:31: FAIL failed == 0
FAIL tests/ipc_test.c:90: connect(fd,(struct sockaddr *)&a,sizeof a)==0
FAIL: make all did not produce build/sublimite
undefined reference to `editor_runtime_display'
undefined reference to `journal_default_dir'
undefined reference to `editor_config_dir'
```

Green run, (M)[AC], BAT0=Not charging, load1=3.24:

```text
x11_identity_test: WM_CLASS, UTF8_STRING _NET_WM_NAME and WM_NAME readback passed on :99
runtime_test: pure display decision honours explicit allow (no environment opt-in)
runtime_test: XDG journal/config and HOME fallback paths passed
cli_test: file window identity, XDG session journals, quiescent trace dump and dump error passed
editor_test: all passed
test_runtime_identity: binary, usage and desktop passed
test_runtime_identity: isolated XDG desktop installation passed
```

Required targets passed. Full sanitizer check (M)[AC], load1=2.54:

```text
make: Nothing to be done for 'all'.
check: 37 test binaries passed
test_replay_cli: all passed
fuzz: 20 fuzzers built
```

Fuzz smoke results, all successful, (M)[AC], shared-box TRACK:

| Fuzzer | Load1 | Runs/time |
|---|---:|---|
| IPC | 8.15 | 1,304,131 / 11 seconds |
| Journal | 8.14 | 2,709 / 11 seconds |
| Trace | 10.61 | 811,179 / 11 seconds |

Bench lines: none; no typing hot path changed.

Open problems: no new failures. Config loading remains unimplemented; its XDG path resolver is provided. LeakSanitizer needs the coordinator’s rerun. `build/edit` and `tools/edit.desktop` are absent. All live runs used Xvfb; the real-display opt-in was never set.