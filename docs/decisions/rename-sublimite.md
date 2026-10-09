# Runtime identity: sublimité — edit-457.18

This applies Tobias's settled product-name paragraph in PRD §11. The product
is **sublimité**; the executable and command are **sublimite** (ASCII).

## Amendment: always lower case

Tobias's amendment (2026-10-09, 14:35, PRD §11 on main) makes the name
always lower case: window title and UTF-8 `_NET_WM_NAME` are `sublimité`,
ASCII `WM_NAME` is `sublimite`, and both NUL-terminated `WM_CLASS` parts
are `sublimite`. The desktop entry uses `Name=sublimité` and
`StartupWMClass=sublimite`. Binary, directories, sockets and CLI usage already
use `sublimite`. The editor's explicit title is updated alongside X11's default.

Tests were changed first, before production strings. The amendment changes
strings only; `src/main.c` (including the display opt-in and shutdown trace
dump) is byte-for-byte unchanged. Font/wordmark work remains deferred.

The Makefile's `EDIT_BIN` and executable rule now produce `build/sublimite`.
The development library name, module symbols/macros, repository name, bead IDs,
historical documentation and `/tmp/edit-corpus` stay unchanged. The artifact
contract is checked on a fresh build; an old worktree must remove its stale
`build/edit` with `make clean` before that check.

`src/x11/x11.c` defaults to the UTF-8 title `sublimité`, writes `_NET_WM_NAME`
with type `UTF8_STRING`, and writes `WM_CLASS` as the two NUL-terminated strings
`sublimite` and `sublimite`. The product title's legacy `WM_NAME` is the ASCII
fallback `sublimite`. Explicit platform titles remain supported. The editor's
window title is the product name even when a file is open (`src/editor/open.c`).

`src/ipc/socket.c` uses `$XDG_RUNTIME_DIR/sublimite-<uid>.sock` and the matching
`.lock`. The abstract fallback is `sublimite-<uid>`; the existing isolation hook
`EDIT_IPC_NAMESPACE` adds its existing `-<namespace>` suffix. Wire formats and
socket ownership/permissions are unchanged. The IPC tests and benchmark cleanup
now expect these names.

`journal_default_dir` in `src/journal/path.c` resolves
`$XDG_DATA_HOME/sublimite`, falling back to `$HOME/.local/share/sublimite`.
`editor_config_dir` in `src/editor/runtime.c` resolves
`$XDG_CONFIG_HOME/sublimite`, falling back to `$HOME/.config/sublimite`.
Empty or relative XDG roots use the HOME fallback, as required for XDG paths;
missing or non-absolute HOME without an absolute XDG root is an error. Resolvers
use caller-owned storage, allocate nothing, create nothing, and return errors
for insufficient capacity. Trailing slashes are normalized.

The CLI (`src/main.c`) creates missing journal-directory parents at startup,
then makes a private `session-XXXXXX` journal there, retaining and reporting it
on exit. Previously it placed an `.edit-journal-XXXXXX` file beside the opened
file. Explicit journal paths passed to the editor API retain their meaning.
Journal checkpoint/backup temporary filenames also use `sublimite`. Existing
journals are not migrated or deleted. The config module does not exist in this
checkout; this bead establishes its path resolver without adding TOML loading.
Internal file-save staging names and test fixture prefixes remain development
shorthand; they do not determine product, command, window, IPC or data identity.

The desktop entry is `tools/sublimite.desktop`, with `Name=sublimité`,
`Exec=sublimite %F`, `TryExec=sublimite`, and `StartupWMClass=sublimite`.
`tools/xdg-install.sh` checks for that command and installs/registers that entry.
`tools/ci.sh` runs the release artifact/desktop contract after `make all`.
The installer test uses isolated XDG storage and stub desktop commands.

## Display opt-in and session traces

`editor_runtime_display(display, safe, allow)` extracts main's redirect
selection as a pure function. Without an opt-in, display zero, an unset display,
and malformed display strings still select a safe `EDIT_DISPLAY` or `:99`.
When `EDIT_ALLOW_REAL_DISPLAY` is present (including an empty value, matching
the test guard), the caller's DISPLAY is preserved. Main applies that choice.
Tests pass literal opt-in values to the pure function; they never set the
real-display opt-in in the environment or connect to the real display.

When `EDIT_TRACE_DUMP=<path>` is set, main opens that path in binary write mode,
calls `trace_dump`, and checks dump/close errors **after `editor_close`** has
joined the worker threads. A write failure reports the path and makes an
otherwise successful session fail. Existing editor errors remain the primary
error. The format is unchanged and readable by `build/tools/tracedump`.
For example, run on the test display, then close the window normally:

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 EDIT_TRACE_DUMP=/tmp/sublimite.trace build/sublimite /path/to/file
build/tools/tracedump /tmp/sublimite.trace
```

## Verification

The tests were written before the rename. Red evidence:

```text
x11_identity_test:18: FAIL xcb_get_property_value_length(r) == (int)len
x11_identity_test:31: FAIL failed == 0
FAIL tests/ipc_test.c:90: connect(fd,(struct sockaddr *)&a,sizeof a)==0
FAIL: make all did not produce build/sublimite
undefined reference to `editor_runtime_display'
undefined reference to `journal_default_dir'
undefined reference to `editor_config_dir'
make: *** [Makefile:87: build/tests/runtime_test] Error 1
```

Green focused evidence:

```text
x11_identity_test: WM_CLASS, UTF8_STRING _NET_WM_NAME and WM_NAME readback passed on :99
ipc_test: parser, wire, roundtrip, --wait, stdin, stale, race, fragments, tokens, endpoints ok
runtime_test: pure display decision honours explicit allow (no environment opt-in)
runtime_test: XDG journal/config and HOME fallback paths passed
cli_test: file window identity, XDG session journals, quiescent trace dump and dump error passed
editor_test: all passed
test_runtime_identity: binary, usage and desktop passed
test_runtime_identity: isolated XDG desktop installation passed
```

Reproduce with:

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 make all
DISPLAY=:99 EDIT_DISPLAY=:99 sh tools/test_runtime_identity.sh
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make check
DISPLAY=:99 EDIT_DISPLAY=:99 make fuzz
```

Live X11 and IPC tests require local socket access outside the Codex sandbox.
LeakSanitizer is disabled for sandbox execution; the coordinator reruns with
leaks enabled. This bead changes startup/identity and no typing hot path, so no
benchmark was rerun, following the repository's bench policy.

Short fuzz smoke runs (M)[AC], BAT0=Not charging, shared-box TRACK; each
requested `-max_total_time=10 -max_len=4096`, ASan/UBSan with leaks disabled:

| Fuzzer | Load1 at start (M)[AC] | Result (M)[AC] |
| --- | --- | --- |
| ipc_fuzz | 8.15 | Done 1304131 runs in 11 second(s) |
| journal_fuzz | 8.14 | Done 2709 runs in 11 second(s) |
| trace_fuzz | 10.61 | Done 811179 runs in 11 second(s) |

Each returned success, without a finding. No benchmark verdict is claimed.

Full verification returned success (M)[AC], BAT0=Not charging; focused green
started at load1 3.24, and the full sanitizer check started at load1 2.54:

```text
make: Nothing to be done for 'all'.
check: 37 test binaries passed
test_replay_cli: all passed
fuzz: 20 fuzzers built
```

The release editor test also confirmed its active allocation guard; the
sanitizer build reports that guard as ASan-inert. No new open failure was found.

## Amendment verification

Red X11 readback, (M)[AC], BAT0=Not charging, load1=7.28:

```text
x11_identity_test:19: FAIL memcmp(xcb_get_property_value(r), want, len) == 0
x11_identity_test:19: FAIL memcmp(xcb_get_property_value(r), want, len) == 0
x11_identity_test:19: FAIL memcmp(xcb_get_property_value(r), want, len) == 0
x11_identity_test:31: FAIL failed == 0
RED x11_identity_test exit=1
```

The failures cover WM_CLASS, _NET_WM_NAME and WM_NAME respectively. The
initial sandbox attempt could not initialize X11; the readback run above used
local socket access outside the sandbox, only against Xvfb :99.

Red desktop contract, (M)[AC], BAT0=Not charging, load1=6.66:

```text
+ grep -q ^Name=sublimité$ tools/sublimite.desktop
RED test_runtime_identity exit=1
```

Focused green, (M)[AC], BAT0=Not charging, load1=9.66:

```text
x11_identity_test: WM_CLASS, UTF8_STRING _NET_WM_NAME and WM_NAME readback passed on :99
runtime_test: pure display decision honours explicit allow (no environment opt-in)
runtime_test: XDG journal/config and HOME fallback paths passed
cli_test: file window identity, XDG session journals, quiescent trace dump and dump error passed
editor_test: all passed
test_runtime_identity: binary, usage and desktop passed
test_runtime_identity: isolated XDG desktop installation passed
focused green exit=0
```

Release build (gcc, strict warnings), (M)[AC], BAT0=Not charging, load1=9.20:

```text
make all exit=0
```

Fuzzer build, (M)[AC], BAT0=Not charging, load1=10.40:

```text
fuzz: 20 fuzzers built
make fuzz exit=0
```

X11 input fuzz smoke, (M)[AC], BAT0=Not charging, load1=12.27, shared-box
TRACK, ASan/UBSan with leaks disabled. Command:
`build/fuzz/x11_input_fuzz -max_total_time=10 -max_len=4096`. No disk corpus
was created or regenerated; /tmp/edit-corpus was untouched.

```text
Done 226513 runs in 11 second(s)
x11_input_fuzz exit=0
```

No finding. No benchmark was run: the amendment changes identity strings
and no typing hot path. All live runs set DISPLAY=:99 and EDIT_DISPLAY=:99
and unset EDIT_ALLOW_REAL_DISPLAY. Main's SHA-256 is unchanged:
`50362449225935e559eef83b3c86658d1474eb7afa6e233956d039edfab6b860`.

Full sanitizer verification (clang ASan/UBSan, detect_leaks=0), (M)[AC],
BAT0=Not charging, load1=10.77 at start:

```text
check: 37 test binaries passed
test_replay_cli: all passed
make check exit=0
```

All required targets returned success. No new open problem was found.
LeakSanitizer remains for the coordinator's rerun with leaks enabled.
