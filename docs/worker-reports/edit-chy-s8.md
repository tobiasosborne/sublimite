# edit-chy worker report

Worktree: `/home/tobias/Projects/sublimite/.wt/edit-chy-s8`.
Only this bead was worked. No `bd` or `git` commands were run. No main checkout,
other worktree, handoff, worklog, plan or requirements file was changed.

## Files touched

- `Makefile`: prefix install/uninstall, release-launcher dependency for CLI
  tests, refusal export in safe display recipes, install test in `check-sh`.
- `README.md`: install, run and uninstall instructions.
- `src/main.c`: default display access, explicit refusal before setup,
  matching Wayland policy, display-free help.
- `src/editor/runtime.c`, `src/editor/runtime.h`: pure refusal-based selector.
- `tests/cli_test.c`: actual release launcher startup and refusal regression.
- `tests/runtime_test.c`: default/refusal selection and Wayland caller policy.
- `tests/display_guard.h`, `tests/x11_display_guard_test.c`: retained test
  redirection and inherited production-child refusal.
- `tools/with_display_lock.sh`, `tools/ci.sh`: child refusal export.
- `tools/sublimite.desktop`, `tools/sublimite.svg`: installed icon identity
  and local monochrome glyph.
- `tools/test_install.sh`: isolated prefix/XDG install, desktop validation,
  binary identity, help, MIME registration and uninstall contract.
- `docs/decisions/edit-chy.md` and this report.

The journal/profile implementation was unchanged. No typing-path allocation,
new error-to-`fail()` path, benchmark, duration or resource budget was added.
The polished wordmark remains outside this bead.

## Red runs

Commands and output are observed evidence, (M)[AC]; source-line numbers are
generated test locations (P), exit values are process statuses (P), and display
identities and shared-box build parallelism come from the brief (P).

Before implementation:

```text
$ sh tools/test_install.sh
make: *** No rule to make target 'install'.  Stop.
exit=2
```

The pure runtime regression was compiled against the unchanged implementation
using the repository's strict warning/sanitizer flags. Section garbage
collection retained only the functions used by this isolated fixture:

```sh
clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -D_GNU_SOURCE -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g \
  -ffunction-sections -fdata-sections -Isrc -Ivendor/tree-sitter/lib/include \
  -include tests/display_guard.h tests/runtime_test.c src/editor/runtime.c \
  src/journal/path.c src/wayland/dispatch.c -Wl,--gc-sections -lm \
  -o build/edit-chy-evidence/runtime_test-red
ASAN_OPTIONS=detect_leaks=1 DISPLAY=:99 build/edit-chy-evidence/runtime_test-red
```

Compile exit was successful; the test failed as intended:

```text
runtime_test:44: FAIL editor_runtime_display(real[i], ":99", NULL) == real[i]
P1.9-2.5: every created directory parent barrier observed; failure stops startup passed
exit=1
```

The original selector already permitted private nonzero displays, so the CLI
assertion that a private `DISPLAY` starts cannot itself demonstrate red. The
pure selector regression demonstrates the changed default without opening a
real display. The CLI contract also exercises explicit refusal.

The sandbox cannot connect to the private Xvfb socket and LeakSanitizer reports
that it cannot inspect threads under ptrace there. The clean red evidence above
was rerun outside the sandbox with leak detection enabled. The private Xvfb
preflight also succeeded outside the sandbox. These are environment constraints,
not waived test assertions.

The actual baseline production launcher was kept intact during the queued
CLI probe. The private startup cases passed; explicit refusal failed because
that launcher redirected to the private display and exited normally instead
of refusing. This is the CLI red run, with leak detection enabled:

```sh
runtime_dir=$(mktemp -d)
trap 'rmdir "$runtime_dir"' EXIT HUP INT TERM
XDG_RUNTIME_DIR="$runtime_dir" DISPLAY=:99 ASAN_OPTIONS=detect_leaks=1 \
  EDIT_DISPLAY_LOCK_TIMEOUT=1800 sh tools/with_display_lock.sh \
  build/san/tests/cli_test --display-policy
```

```text
Not charging
 10:04:07 up 6 days, 14:52,  1 user,  load average: 51.11, 44.15, 44.81
waiting for display :99 (held by 2249676)
cli_test:155: FAIL !window && guarded && WIFEXITED(status) && WEXITSTATUS(status) == EXIT_FAILURE
cli_test: default production launch uses DISPLAY without an opt-in or alternate
cli_test: default production launch uses DISPLAY without an opt-in or alternate
Not charging
 10:17:37 up 6 days, 15:05,  1 user,  load average: 48.53, 47.24, 46.11
cli policy red exit=1
```

## Green runs

The following output is (M)[AC], with power/load stamps adjacent to the runs.
Display names, inherited fixture watchdogs, protocol constants and diagnostic
section identifiers come from the existing tests or the brief (P). No new
measurement target or budget was introduced.

The sanitizer policy suites ran with `ASAN_OPTIONS=detect_leaks=1`, private
`XDG_RUNTIME_DIR`, `DISPLAY=:99`, and `EDIT_DISPLAY_LOCK_TIMEOUT=1800`, each via
`sh tools/with_display_lock.sh build/san/tests/<name>`:

```text
Not charging
 10:11:23 up 6 days, 14:59,  1 user,  load average: 43.70, 44.99, 45.21
== runtime_test
waiting for display :99 (held by 2251901)
P1.9-2.5: every created directory parent barrier observed; failure stops startup passed
runtime_test: default display preserved; explicit refusal selects private fallback; Wayland caller policy passed
runtime_test: XDG journal/config and HOME fallback paths passed
runtime_test exit=0
== x11_display_guard_test
waiting for display :99 (held by 2260947)
display guard: real display hostname/unix:0 refused, using :99
display guard: real display localhost:00.0 refused, using :99
display_guard_test: §39 ok
x11_display_guard_test exit=0
Not charging
 10:12:39 up 6 days, 15:00,  1 user,  load average: 46.96, 45.73, 45.45
```

The full sanitizer CLI suite used the same environment and display lock.
Its forced startup timeout and trace-dump error are intentional negative
fixtures; the suite checks those failures and exits successfully. Xvfb's EGL
failure exercised the existing raster fallback.

```text
Not charging
 10:18:52 up 6 days, 15:06,  1 user,  load average: 50.94, 48.23, 46.54
waiting for display :99 (held by 2281930)
cli_test: decoy display=:99 launcher=:99 shared runtime regression
cli_test: FAIL: window not created within 0 s (load1=49.02) [forced test timeout]
cli_test:483: FAIL ran == 0
cli_test: §36 forced startup timeout correctly failed
cli_test: decoy display=:99 launcher=:99 shared runtime regression
sublimite: EGL init failed: EGL/GL/Present unavailable (code=-10); using raster; no retry
sublimite: unsaved session journal: /tmp/sublimite-cli-LzsBmg/sublimite/session-Yp6RN2
cli_test: §37 production startup trace endpoints passed
cli_test: different-display decoy, shared runtime regression
sublimite: EGL init failed: EGL/GL/Present unavailable (code=-10); using raster; no retry
sublimite: unsaved session journal: /tmp/sublimite-cli-LzsBmg/sublimite/session-v9ZBXu
cli_test: §37 production startup trace endpoints passed
sublimite: EGL init failed: EGL/GL/Present unavailable (code=-10); using raster; no retry
sublimite: cannot write trace dump: /tmp/sublimite-cli-LzsBmg
sublimite: error -103; journal: /tmp/sublimite-cli-LzsBmg/sublimite/session-M6taqM
sublimite: EGL init failed: EGL/GL/Present unavailable (code=-10); using raster; no retry
sublimite: EGL init failed: EGL/GL/Present unavailable (code=-10); using raster; no retry
sublimite: unsaved session journal: /tmp/sublimite-cli-LzsBmg/sublimite/session-3Me2ZF
cli_test: decoy display=:99 launcher=:99 shared runtime regression
sublimite: unsaved session journal: /tmp/sublimite-cli-A6LNUZ/sublimite/session-HgFO8a
cli_test: §36 delayed production backend initialization completed
cli_test: §37 production startup trace endpoints passed
cli_test: different-display decoy, shared runtime regression
sublimite: unsaved session journal: /tmp/sublimite-cli-A6LNUZ/sublimite/session-3zu0h0
cli_test: §36 delayed production backend initialization completed
cli_test: §37 production startup trace endpoints passed
sublimite: cannot write trace dump: /tmp/sublimite-cli-A6LNUZ
sublimite: error -103; journal: /tmp/sublimite-cli-A6LNUZ/sublimite/session-XdHFmk
cli_test: default production launch uses DISPLAY without an opt-in or alternate
cli_test: legacy display opt-in remains compatible
cli_test: explicit real-display refusal exits with guard diagnostic
edit-97i: production CLI Unicode/alias hand-off reuses tab; independent --wait clients finish on its close
cli_test: same-display launcher handoff preserved; child_exit=0 handoffs=1
cli_test: decoy bypassed; child window created; handoffs=0
cli_test: decoy bypassed; child window created; handoffs=0
cli_test: decoy bypassed; child window created; handoffs=0
cli_test: SIGKILL after native typing; production relaunch restores marker/cursor/selection/scroll and actual X window geometry (G: bead display trial)
cli_test: file window identity, XDG session journals, quiescent trace dump and dump error passed
cli_test: same-display launcher handoff preserved; child_exit=0 handoffs=1
cli_test: decoy bypassed; child window created; handoffs=0
cli_test: decoy bypassed; child window created; handoffs=0
cli_test: decoy bypassed; child window created; handoffs=0
cli_test: file window identity, XDG session journals, quiescent trace dump and dump error passed
Not charging
 10:26:46 up 6 days, 15:14,  1 user,  load average: 49.18, 50.58, 48.82
cli_test green exit=0
```

Install contract, using isolated XDG configuration and a temporary prefix:

```sh
sh tools/test_install.sh
```

```text
install -d "/tmp/tmp.wbuVJAZqtm/prefix/bin" "/tmp/tmp.wbuVJAZqtm/prefix/share/applications" "/tmp/tmp.wbuVJAZqtm/prefix/share/icons/hicolor/scalable/apps"
install -m 755 build/sublimite "/tmp/tmp.wbuVJAZqtm/prefix/bin/sublimite"
install -m 755 build/tools/tracedump "/tmp/tmp.wbuVJAZqtm/prefix/bin/tracedump"
install -m 644 tools/sublimite.desktop "/tmp/tmp.wbuVJAZqtm/prefix/share/applications/sublimite.desktop"
install -m 644 tools/sublimite.svg "/tmp/tmp.wbuVJAZqtm/prefix/share/icons/hicolor/scalable/apps/sublimite.svg"
gtk-update-icon-cache: Cache file created successfully.
Installed:
/tmp/tmp.wbuVJAZqtm/prefix/bin/sublimite
/tmp/tmp.wbuVJAZqtm/prefix/bin/tracedump
/tmp/tmp.wbuVJAZqtm/prefix/share/applications/sublimite.desktop
/tmp/tmp.wbuVJAZqtm/prefix/share/icons/hicolor/scalable/apps/sublimite.svg
/tmp/tmp.wbuVJAZqtm/prefix/share/applications/sublimite.desktop: hint: value "Utility;TextEditor;Development;" for key "Categories" in group "Desktop Entry" contains more than one main category; application might appear more than once in the application menu
usage: sublimite [--wait] [--new-instance] [--font-family PATTERN] [--font-file PATH[#INDEX]] [--font-fallbacks A,B] [--] [path[:line[:col]] ...] [-]
rm -f "/tmp/tmp.wbuVJAZqtm/prefix/bin/sublimite" "/tmp/tmp.wbuVJAZqtm/prefix/bin/tracedump" "/tmp/tmp.wbuVJAZqtm/prefix/share/applications/sublimite.desktop" "/tmp/tmp.wbuVJAZqtm/prefix/share/icons/hicolor/scalable/apps/sublimite.svg"
test_install: release binaries, desktop validation, icon, help, MIME default and uninstall passed
exit=0
```

Required fixed-prefix smoke, also with isolated XDG configuration:

```text
install -d "/tmp/chy-prefix/bin" "/tmp/chy-prefix/share/applications" "/tmp/chy-prefix/share/icons/hicolor/scalable/apps"
install -m 755 build/sublimite "/tmp/chy-prefix/bin/sublimite"
install -m 755 build/tools/tracedump "/tmp/chy-prefix/bin/tracedump"
install -m 644 tools/sublimite.desktop "/tmp/chy-prefix/share/applications/sublimite.desktop"
install -m 644 tools/sublimite.svg "/tmp/chy-prefix/share/icons/hicolor/scalable/apps/sublimite.svg"
gtk-update-icon-cache: Cache file created successfully.
Installed:
/tmp/chy-prefix/bin/sublimite
/tmp/chy-prefix/bin/tracedump
/tmp/chy-prefix/share/applications/sublimite.desktop
/tmp/chy-prefix/share/icons/hicolor/scalable/apps/sublimite.svg
fixed-prefix install exit=0
usage: sublimite [--wait] [--new-instance] [--font-family PATTERN] [--font-file PATH[#INDEX]] [--font-fallbacks A,B] [--] [path[:line[:col]] ...] [-]
installed help exit=0
rm -f "/tmp/chy-prefix/bin/sublimite" "/tmp/chy-prefix/bin/tracedump" "/tmp/chy-prefix/share/applications/sublimite.desktop" "/tmp/chy-prefix/share/icons/hicolor/scalable/apps/sublimite.svg"
fixed-prefix uninstall exit=0
```

## Verification commands and exit codes

Exit codes below are observed command results, (M)[AC]. Compiler dialect,
warning and sanitizer settings are the repository defaults required by the
brief (P). The shared-box parallelism and display-lock timeout are from the
brief (P), not new estimates. All builds used at most the requested make
parallelism.

| Command | Exit |
| --- | --- |
| `make -j3 all` | `0` |
| `make -j3 build/san/tests/cli_test build/san/tests/runtime_test build/san/tests/x11_display_guard_test` | `0` |
| `XDG_RUNTIME_DIR="$runtime_dir" DISPLAY=:99 ASAN_OPTIONS=detect_leaks=1 EDIT_DISPLAY_LOCK_TIMEOUT=1800 sh tools/with_display_lock.sh build/san/tests/runtime_test` | `0` |
| Same wrapper/environment for `build/san/tests/x11_display_guard_test` | `0` |
| Same wrapper/environment for the full `build/san/tests/cli_test` | `0` |
| `sh tools/test_install.sh` | `0` |
| `make install PREFIX=/tmp/chy-prefix` | `0` |
| `/tmp/chy-prefix/bin/sublimite --help` | `0` |
| `make uninstall PREFIX=/tmp/chy-prefix` | `0` |
| `sh -n tools/test_install.sh tools/with_display_lock.sh tools/ci.sh`, followed by `desktop-file-validate tools/sublimite.desktop` | `0` |
| `env -u EDIT_REFUSE_REAL_DISPLAY DISPLAY=:99 EDIT_DISPLAY_LOCK_TIMEOUT=1800 sh tools/with_display_lock.sh sh -c 'test "$EDIT_REFUSE_REAL_DISPLAY" = 1'` | `0` |

Preparatory builds also exited successfully: the initial release/sanitizer CLI
build, `make -j3 -o build/sublimite all` (holding the baseline executable for
red), the policy sanitizer targets, and the final entry-point objects. The
ordinary `make -j3 all` above then linked the final launcher; no target was
ignored in that final verification.

Full build logs and unabridged run evidence are in
`build/edit-chy-evidence/`. No benches or full `check-sh` were run; the scoped
sanitizer suites and install script are the verification requested by the
brief. Install itself neither ran nor built tests or benches. Temporary
prefixes, runtime directories and the unused baseline snapshot were removed.
No GB text fixture was created. No real display connection was made: the
refusal probe names the real display only with refusal enabled, and the old
launcher redirected that probe to the private Xvfb.

## What remains and findings outside scope

No in-scope implementation or required verification remains. Actual default
prefix installation was not performed in the user's home; verification used
temporary prefixes. The default bin directory is already on this tool shell's
PATH. The journal/profile path implementation and the user's profile were
unchanged. The polished wordmark and packaging for other machines remain
outside this bead.

Observed outside scope, left unchanged:

- `desktop-file-validate` reports a hint about the existing desktop entry's
  multiple main categories. Validation still succeeds.
- `tools/test_runtime_identity.sh`, invoked by CI, still expects a no-argument
  usage exit and the old terse `usage: sublimite <file>` text. The existing
  launcher already accepts empty launches and uses the longer usage text.
  That helper was read, not run or repaired in this bead. CI's refusal export
  was added as requested; no broader CI changes were made.
